/*
  ==============================================================================

    Studio Verb - Professional Reverb Plugin
    Copyright (c) 2024 Luna Co. Audio

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DattorroReverb.h"
#include "FreverbAlgorithm.h"

//==============================================================================
StudioVerbAudioProcessor::StudioVerbAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       parameters(*this, nullptr, "Parameters", createParameterLayout())
{
    // Initialize with default values first - lightweight operations only
    currentAlgorithm.store(Plate);
    currentSize.store(0.5f);
    currentDamp.store(0.5f);
    currentPredelay.store(20.0f);
    currentMix.store(0.3f);
    currentWidth.store(1.0f);

    // Don't create the reverb engines in constructor - defer to prepareToPlay
    // This avoids heavy initialization during plugin scanning
    dattorroReverb = nullptr;
    freeverb = nullptr;

    // Initialize factory presets - lightweight
    initializePresets();

    // Add parameter listeners for core parameters only
    parameters.addParameterListener(ALGORITHM_ID, this);
    parameters.addParameterListener(SIZE_ID, this);
    parameters.addParameterListener(DAMP_ID, this);
    parameters.addParameterListener(PREDELAY_ID, this);
    parameters.addParameterListener(MIX_ID, this);
    parameters.addParameterListener(WIDTH_ID, this);
}

StudioVerbAudioProcessor::~StudioVerbAudioProcessor()
{
    parameters.removeParameterListener(ALGORITHM_ID, this);
    parameters.removeParameterListener(SIZE_ID, this);
    parameters.removeParameterListener(DAMP_ID, this);
    parameters.removeParameterListener(PREDELAY_ID, this);
    parameters.removeParameterListener(MIX_ID, this);
    parameters.removeParameterListener(WIDTH_ID, this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout StudioVerbAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Algorithm selector - 3 clean, proven algorithms
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ALGORITHM_ID,
        "Algorithm",
        juce::StringArray { "Plate", "Room", "Hall" },
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

    return layout;
}

//==============================================================================
void StudioVerbAudioProcessor::initializePresets()
{
    // Simple presets using only Plate, Room, Hall algorithms
    factoryPresets.push_back({ "Small Room", Room, 0.3f, 0.5f, 8.0f, 0.25f, 0.6f });
    factoryPresets.push_back({ "Medium Room", Room, 0.5f, 0.4f, 12.0f, 0.35f, 0.7f });
    factoryPresets.push_back({ "Large Room", Room, 0.7f, 0.35f, 18.0f, 0.40f, 0.8f });

    factoryPresets.push_back({ "Small Hall", Hall, 0.5f, 0.4f, 18.0f, 0.30f, 0.7f });
    factoryPresets.push_back({ "Medium Hall", Hall, 0.7f, 0.35f, 25.0f, 0.35f, 0.8f });
    factoryPresets.push_back({ "Large Hall", Hall, 0.85f, 0.3f, 32.0f, 0.40f, 0.9f });

    factoryPresets.push_back({ "Bright Plate", Plate, 0.4f, 0.15f, 8.0f, 0.35f, 0.85f });
    factoryPresets.push_back({ "Vintage Plate", Plate, 0.6f, 0.4f, 10.0f, 0.40f, 0.8f });
    factoryPresets.push_back({ "Dark Plate", Plate, 0.65f, 0.65f, 12.0f, 0.38f, 0.75f });
}

//==============================================================================
void StudioVerbAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // Thread safety: Lock to prevent artifacts during audio processing
    const juce::ScopedLock sl(processLock);

    // Input validation and bounds checking - only handle core 6 parameters
    if (parameterID == ALGORITHM_ID)
    {
        int algorithmInt = static_cast<int>(newValue);
        algorithmInt = juce::jlimit(0, static_cast<int>(NumAlgorithms) - 1, algorithmInt);
        currentAlgorithm.store(static_cast<Algorithm>(algorithmInt));
    }
    else if (parameterID == SIZE_ID)
    {
        currentSize.store(juce::jlimit(0.0f, 1.0f, newValue));
    }
    else if (parameterID == DAMP_ID)
    {
        currentDamp.store(juce::jlimit(0.0f, 1.0f, newValue));
    }
    else if (parameterID == PREDELAY_ID)
    {
        currentPredelay.store(juce::jlimit(0.0f, 200.0f, newValue));
    }
    else if (parameterID == MIX_ID)
    {
        currentMix.store(juce::jlimit(0.0f, 1.0f, newValue));
    }
    else if (parameterID == WIDTH_ID)
    {
        currentWidth.store(juce::jlimit(0.0f, 1.0f, newValue));
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
        // Update only core 6 parameters
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
    // Simple fallback - no complex tail calculation
    return 5.0;
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

    // Lazy initialization - create reverb engines on first prepareToPlay
    // This avoids heavy initialization during plugin scanning
    if (!dattorroReverb)
    {
        DBG("StudioVerb: Creating Dattorro reverb engine in prepareToPlay");
        dattorroReverb = std::make_unique<DattorroReverb>();
    }

    if (!freeverb)
    {
        DBG("StudioVerb: Creating Freeverb engine in prepareToPlay");
        freeverb = std::make_unique<FreverbAlgorithm>();
    }

    // Disable denormalized number support to prevent CPU spikes
    juce::FloatVectorOperations::disableDenormalisedNumberSupport(true);

    // Prepare both reverb engines
    dattorroReverb->prepare(sampleRate, samplesPerBlock);
    freeverb->prepare(sampleRate, samplesPerBlock);

    // Reset to clear any previous state and prevent artifacts
    dattorroReverb->reset();
    freeverb->reset();
}

void StudioVerbAudioProcessor::releaseResources()
{
    // Clear reverb state when stopping playback
    if (dattorroReverb)
        dattorroReverb->reset();
    if (freeverb)
        freeverb->reset();
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

    // Critical buffer validation
    if (buffer.getNumChannels() < 2 || buffer.getNumSamples() == 0)
        return;

    // Validate engines exist
    if (!dattorroReverb || !freeverb)
        return;

    // Handle mono input by duplicating to stereo
    if (getTotalNumInputChannels() == 1)
    {
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    }

    // Get current parameters
    Algorithm algo = currentAlgorithm.load();
    float size = currentSize.load();
    float damp = currentDamp.load();
    float predelay = currentPredelay.load();
    float mix = currentMix.load();
    float width = currentWidth.load();

    // Get input/output channels
    auto* channelDataL = buffer.getWritePointer(0);
    auto* channelDataR = buffer.getWritePointer(1);

    // Process sample by sample (simple but stable)
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float inL = channelDataL[sample];
        float inR = channelDataR[sample];

        // Store dry signal
        float dryL = inL;
        float dryR = inR;

        // Process based on algorithm
        float wetL = 0.0f, wetR = 0.0f;

        if (algo == Plate)
        {
            // Dattorro plate reverb
            // Map Size to decay: 0.85-0.999 for lush plate reverb
            float decay = 0.85f + (size * 0.149f);
            dattorroReverb->process(inL, inR, wetL, wetR, size, decay, damp, predelay);
        }
        else // Room or Hall
        {
            // Freeverb - Map Size to both room size and decay
            // Room uses smaller sizes (0-1), Hall uses larger (0.3-1.0)
            float scaledSize = (algo == Hall) ? (size * 0.7f + 0.3f) : size;
            // Higher decay for more reverb tail: 0.9-0.999
            float decay = 0.9f + (size * 0.099f);
            freeverb->process(inL, inR, wetL, wetR, scaledSize, decay, damp, predelay);
        }

        // Stereo width control
        float mid = (wetL + wetR) * 0.5f;
        float side = (wetL - wetR) * 0.5f * width;
        wetL = mid + side;
        wetR = mid - side;

        // Dry/wet mix
        channelDataL[sample] = dryL * (1.0f - mix) + wetL * mix;
        channelDataR[sample] = dryR * (1.0f - mix) + wetR * mix;
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
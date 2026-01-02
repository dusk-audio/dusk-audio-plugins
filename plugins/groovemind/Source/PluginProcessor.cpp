/*
  ==============================================================================

    GrooveMind - ML-Powered Intelligent Drummer
    PluginProcessor.cpp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
GrooveMindProcessor::GrooveMindProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                     .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()),
      patternLibrary(),
      drummerEngine(patternLibrary),
      grooveHumanizer(),
      followModeController()
{
    // Load pattern library
    loadPatternLibrary();

    // Load ML models
    loadMLModels();
}

void GrooveMindProcessor::loadPatternLibrary()
{
    // Try multiple locations for the pattern library
    juce::StringArray searchPaths;

    // 1. Relative to the plugin binary (for installed plugins)
    auto pluginFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    searchPaths.add(pluginFile.getParentDirectory().getChildFile("GrooveMind_Patterns").getFullPathName());

    // 2. User's home directory (standard location)
    searchPaths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getChildFile(".local/share/GrooveMind/patterns").getFullPathName());

    // 3. Development location (groovemind-training/library)
    // Go up from plugin directory to find groovemind-training
    auto devPath = pluginFile.getParentDirectory();
    for (int i = 0; i < 6; ++i)  // Search up to 6 levels
    {
        auto trainPath = devPath.getChildFile("groovemind-training/library");
        if (trainPath.isDirectory())
        {
            searchPaths.add(trainPath.getFullPathName());
            break;
        }
        devPath = devPath.getParentDirectory();
    }

    // 4. Hardcoded development path (fallback)
    searchPaths.add("/home/marc/projects/plugins/groovemind-training/library");

    // Try each path
    for (const auto& path : searchPaths)
    {
        juce::File libraryDir(path);
        if (libraryDir.isDirectory())
        {
            if (patternLibrary.loadFromDirectory(libraryDir))
            {
                DBG("GrooveMind: Loaded " + juce::String(patternLibrary.getPatternCount()) +
                    " patterns from " + path);
                return;
            }
        }
    }

    DBG("GrooveMind: WARNING - No pattern library found! Searched:");
    for (const auto& path : searchPaths)
        DBG("  - " + path);
}

//==============================================================================
juce::File GrooveMindProcessor::getResourcesDirectory() const
{
    // Try multiple locations for ML models and resources
    juce::StringArray searchPaths;

    // 1. Relative to plugin binary (installed plugins)
    auto pluginFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    searchPaths.add(pluginFile.getParentDirectory().getChildFile("GrooveMind_Resources").getFullPathName());

    // 2. User's home directory (standard location)
    searchPaths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getChildFile(".local/share/GrooveMind/models").getFullPathName());

    // 3. Development location (groovemind-training/rtneural)
    auto devPath = pluginFile.getParentDirectory();
    for (int i = 0; i < 6; ++i)
    {
        auto trainPath = devPath.getChildFile("groovemind-training/rtneural");
        if (trainPath.isDirectory())
        {
            searchPaths.add(trainPath.getFullPathName());
            break;
        }
        devPath = devPath.getParentDirectory();
    }

    // 4. Plugin Source Resources directory (development)
    devPath = pluginFile.getParentDirectory();
    for (int i = 0; i < 6; ++i)
    {
        auto resPath = devPath.getChildFile("plugins/groovemind/Resources");
        if (resPath.isDirectory())
        {
            searchPaths.add(resPath.getFullPathName());
            break;
        }
        devPath = devPath.getParentDirectory();
    }

    // 5. Hardcoded development paths (fallback)
    searchPaths.add("/home/marc/projects/plugins/groovemind-training/rtneural");
    searchPaths.add("/home/marc/projects/plugins/plugins/groovemind/Resources");

    // Find first valid directory
    for (const auto& path : searchPaths)
    {
        juce::File dir(path);
        if (dir.isDirectory())
        {
            // Check if it contains at least one expected model file
            if (dir.getChildFile("humanizer.json").existsAsFile() ||
                dir.getChildFile("style_classifier.json").existsAsFile() ||
                dir.getChildFile("timing_stats.json").existsAsFile())
            {
                return dir;
            }
        }
    }

    // Return empty file if not found
    DBG("GrooveMind: WARNING - No ML models directory found!");
    return juce::File();
}

void GrooveMindProcessor::loadMLModels()
{
    auto resourcesDir = getResourcesDirectory();

    if (!resourcesDir.isDirectory())
    {
        DBG("GrooveMind: ML models directory not found - using statistical humanization only");
        return;
    }

    DBG("GrooveMind: Loading ML models from " + resourcesDir.getFullPathName());

    // Load humanizer model
    auto humanizerFile = resourcesDir.getChildFile("humanizer.json");
    if (humanizerFile.existsAsFile())
    {
        if (grooveHumanizer.loadModel(humanizerFile))
            DBG("GrooveMind: Humanizer model loaded");
        else
            DBG("GrooveMind: Failed to load humanizer model");
    }

    // Load timing statistics
    auto timingStatsFile = resourcesDir.getChildFile("timing_stats.json");
    if (timingStatsFile.existsAsFile())
    {
        if (grooveHumanizer.loadTimingStats(timingStatsFile))
            DBG("GrooveMind: Timing statistics loaded");
        else
            DBG("GrooveMind: Failed to load timing statistics");
    }

    // Load style classifier
    auto styleClassifierFile = resourcesDir.getChildFile("style_classifier.json");
    if (styleClassifierFile.existsAsFile())
    {
        if (drummerEngine.loadStyleClassifier(styleClassifierFile))
            DBG("GrooveMind: Style classifier loaded");
        else
            DBG("GrooveMind: Failed to load style classifier");
    }

    // Report ML status
    if (grooveHumanizer.isModelLoaded())
        DBG("GrooveMind: ML humanization enabled");
    else
        DBG("GrooveMind: Using statistical humanization (ML model not available)");

    if (drummerEngine.isMLEnabled())
        DBG("GrooveMind: ML pattern selection enabled");
    else
        DBG("GrooveMind: Using query-based pattern selection (ML model not available)");
}

GrooveMindProcessor::~GrooveMindProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout GrooveMindProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Style selection
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "style", "Style",
        juce::StringArray{"Rock", "Pop", "Funk", "Soul", "Jazz", "Blues",
                          "HipHop", "R&B", "Electronic", "Latin", "Country", "Punk"},
        0));

    // Drummer personality
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "drummer", "Drummer",
        juce::StringArray{"Alex - Versatile", "Jordan - Groovy", "Sam - Steady",
                          "Riley - Energetic", "Casey - Technical", "Morgan - Jazz"},
        0));

    // Kit type
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "kit", "Kit Type",
        juce::StringArray{"Acoustic", "Brush", "Electronic", "Hybrid"},
        0));

    // Section
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "section", "Section",
        juce::StringArray{"Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Breakdown", "Outro"},
        1));

    // XY Pad controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "complexity", "Complexity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "loudness", "Loudness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    // Energy
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "energy", "Energy",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.6f));

    // Groove amount (humanization)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "groove", "Groove",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.7f));

    // Swing
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "swing", "Swing",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    // Fill controls
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "fill_mode", "Fill Mode",
        juce::StringArray{"Auto", "Manual", "Off"},
        0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fill_intensity", "Fill Intensity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "fill_length", "Fill Length",
        juce::StringArray{"1 Beat", "2 Beats", "1 Bar", "2 Bars"},
        2));

    // Instrument toggles
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "kick_enabled", "Kick", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "snare_enabled", "Snare", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "hihat_enabled", "Hi-Hat", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "toms_enabled", "Toms", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "cymbals_enabled", "Cymbals", true));

    // Follow mode
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "follow_enabled", "Follow Mode", false));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String GrooveMindProcessor::getName() const
{
    return JucePlugin_Name;
}

bool GrooveMindProcessor::acceptsMidi() const { return true; }
bool GrooveMindProcessor::producesMidi() const { return true; }
bool GrooveMindProcessor::isMidiEffect() const { return false; }
double GrooveMindProcessor::getTailLengthSeconds() const { return 0.0; }

int GrooveMindProcessor::getNumPrograms() { return 1; }
int GrooveMindProcessor::getCurrentProgram() { return 0; }
void GrooveMindProcessor::setCurrentProgram(int) {}
const juce::String GrooveMindProcessor::getProgramName(int) { return {}; }
void GrooveMindProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void GrooveMindProcessor::prepareToPlay(double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    drummerEngine.prepare(sampleRate, samplesPerBlock);
    grooveHumanizer.prepare(sampleRate);
    followModeController.prepare(sampleRate, samplesPerBlock);
}

void GrooveMindProcessor::releaseResources()
{
}

bool GrooveMindProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Output must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Input (sidechain) can be stereo or disabled
    auto sidechainSet = layouts.getMainInputChannelSet();
    if (!sidechainSet.isDisabled() && sidechainSet != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void GrooveMindProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Get transport info from host
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            transportPlaying = position->getIsPlaying();

            if (auto bpm = position->getBpm())
                currentBPM = *bpm;

            if (auto ppq = position->getPpqPosition())
                currentPositionBeats = *ppq;
        }
    }

    // Get follow mode parameter
    bool followEnabled = apvts.getRawParameterValue("follow_enabled")->load() > 0.5f;

    // Process sidechain input for Follow Mode
    if (followEnabled && buffer.getNumChannels() >= 2)
    {
        followModeController.setEnabled(true);
        followModeController.processAudio(
            buffer.getReadPointer(0),
            buffer.getReadPointer(1),
            buffer.getNumSamples(),
            currentBPM,
            currentPositionBeats);
    }
    else
    {
        followModeController.setEnabled(false);
    }

    // Clear audio output (we only produce MIDI)
    buffer.clear();

    // Only generate patterns if transport is playing
    if (!transportPlaying)
    {
        midiMessages.clear();
        return;
    }

    // Get current parameters
    int styleIndex = static_cast<int>(apvts.getRawParameterValue("style")->load());
    int drummerIndex = static_cast<int>(apvts.getRawParameterValue("drummer")->load());
    int sectionIndex = static_cast<int>(apvts.getRawParameterValue("section")->load());
    int kitIndex = static_cast<int>(apvts.getRawParameterValue("kit")->load());

    float complexity = apvts.getRawParameterValue("complexity")->load();
    float loudness = apvts.getRawParameterValue("loudness")->load();
    float energy = apvts.getRawParameterValue("energy")->load();
    float groove = apvts.getRawParameterValue("groove")->load();
    float swing = apvts.getRawParameterValue("swing")->load();

    // Update drummer engine parameters
    drummerEngine.setStyle(styleIndex);
    drummerEngine.setDrummer(drummerIndex);
    drummerEngine.setSection(sectionIndex);
    drummerEngine.setKit(kitIndex);
    drummerEngine.setComplexity(complexity);
    drummerEngine.setLoudness(loudness);
    drummerEngine.setEnergy(energy);

    // Process and generate MIDI
    drummerEngine.process(buffer.getNumSamples(), currentBPM, currentPositionBeats, midiMessages);

    // Apply humanization (with optional Follow Mode groove application)
    if (groove > 0.01f)
    {
        grooveHumanizer.setGrooveAmount(groove);
        grooveHumanizer.setSwing(swing);
        grooveHumanizer.process(midiMessages, currentBPM);
    }

    // Apply extracted groove from Follow Mode if active
    if (followEnabled && followModeController.getExtractedGroove().isValid)
    {
        // The follow mode controller can modify timing/velocity based on extracted groove
        // This is applied on top of the standard humanization
        juce::MidiBuffer adjustedBuffer;

        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            int samplePos = metadata.samplePosition;

            if (msg.isNoteOn())
            {
                // Calculate beat position
                double beatPosition = currentPositionBeats +
                    (samplePos / sampleRate) * (currentBPM / 60.0);

                // Apply groove timing offset
                float timingOffsetMs = followModeController.applyGroove(beatPosition, 0.0f);
                int offsetSamples = static_cast<int>(timingOffsetMs * sampleRate / 1000.0);
                int newSamplePos = juce::jmax(0, samplePos + offsetSamples);

                // Apply groove velocity
                int originalVelocity = msg.getVelocity();
                float adjustedVelocity = followModeController.applyGrooveVelocity(
                    beatPosition, static_cast<float>(originalVelocity));
                int newVelocity = juce::jlimit(1, 127, static_cast<int>(adjustedVelocity));

                adjustedBuffer.addEvent(
                    juce::MidiMessage::noteOn(msg.getChannel(), msg.getNoteNumber(),
                                               static_cast<juce::uint8>(newVelocity)),
                    newSamplePos);
            }
            else
            {
                adjustedBuffer.addEvent(msg, samplePos);
            }
        }

        midiMessages.swapWith(adjustedBuffer);
    }
}

//==============================================================================
bool GrooveMindProcessor::isFollowModeEnabled() const
{
    return apvts.getRawParameterValue("follow_enabled")->load() > 0.5f;
}

bool GrooveMindProcessor::isFollowModeActive() const
{
    return isFollowModeEnabled() && followModeController.getExtractedGroove().isValid;
}

//==============================================================================
bool GrooveMindProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* GrooveMindProcessor::createEditor()
{
    return new GrooveMindEditor(*this);
}

//==============================================================================
void GrooveMindProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GrooveMindProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveMindProcessor();
}

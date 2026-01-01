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
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()),
      patternLibrary(),
      drummerEngine(patternLibrary),
      grooveHumanizer()
{
    // Load pattern library
    loadPatternLibrary();
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
}

void GrooveMindProcessor::releaseResources()
{
}

bool GrooveMindProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // This plugin produces MIDI, audio buses are mainly for pass-through
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void GrooveMindProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Clear audio (we only produce MIDI)
    buffer.clear();

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

    // Apply humanization
    if (groove > 0.01f)
    {
        grooveHumanizer.setGrooveAmount(groove);
        grooveHumanizer.setSwing(swing);
        grooveHumanizer.process(midiMessages, currentBPM);
    }
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

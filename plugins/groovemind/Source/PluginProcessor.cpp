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
                     .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),  // Sidechain only - no audio output (MIDI generator)
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
    juce::StringArray searchPaths;

    auto pluginFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    searchPaths.add(pluginFile.getParentDirectory().getChildFile("GrooveMind_Patterns").getFullPathName());

    searchPaths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getChildFile(".local/share/GrooveMind/patterns").getFullPathName());

    // Walk up from plugin binary to find groovemind-training/library
    auto devPath = pluginFile.getParentDirectory();
    for (int i = 0; i < 6; ++i)
    {
        auto trainPath = devPath.getChildFile("groovemind-training/library");
        if (trainPath.isDirectory())
        {
            searchPaths.add(trainPath.getFullPathName());
            break;
        }
        devPath = devPath.getParentDirectory();
    }

    for (const auto& path : searchPaths)
    {
        juce::File libraryDir(path);
        if (libraryDir.isDirectory() && patternLibrary.loadFromDirectory(libraryDir))
            return;
    }

    DBG("GrooveMind: No pattern library found");
}

//==============================================================================
juce::File GrooveMindProcessor::getResourcesDirectory() const
{
    juce::StringArray searchPaths;

    auto pluginFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    searchPaths.add(pluginFile.getParentDirectory().getChildFile("GrooveMind_Resources").getFullPathName());

    searchPaths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getChildFile(".local/share/GrooveMind/models").getFullPathName());

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

    for (const auto& path : searchPaths)
    {
        juce::File dir(path);
        if (dir.isDirectory() &&
            (dir.getChildFile("humanizer.json").existsAsFile() ||
             dir.getChildFile("style_classifier.json").existsAsFile() ||
             dir.getChildFile("timing_stats.json").existsAsFile()))
        {
            return dir;
        }
    }

    return juce::File();
}

void GrooveMindProcessor::loadMLModels()
{
    auto resourcesDir = getResourcesDirectory();
    if (!resourcesDir.isDirectory())
        return;

    auto humanizerFile = resourcesDir.getChildFile("humanizer.json");
    if (humanizerFile.existsAsFile())
        grooveHumanizer.loadModel(humanizerFile);

    auto timingStatsFile = resourcesDir.getChildFile("timing_stats.json");
    if (timingStatsFile.existsAsFile())
        grooveHumanizer.loadTimingStats(timingStatsFile);

    auto styleClassifierFile = resourcesDir.getChildFile("style_classifier.json");
    if (styleClassifierFile.existsAsFile())
        drummerEngine.loadStyleClassifier(styleClassifierFile);
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

    // Follow mode amount (how much the analysis affects drum parameters)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "follow_amount", "Follow Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.7f));

    // Follow mode sensitivity (how responsive to input dynamics)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "follow_sensitivity", "Follow Sensitivity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

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
    audioAnalyzer.prepare(sampleRate, samplesPerBlock);
}

void GrooveMindProcessor::releaseResources()
{
}

bool GrooveMindProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // No audio output - this is a MIDI-only generator
    if (!layouts.getMainOutputChannelSet().isDisabled())
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

    // Transport info
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

    bool followEnabled = apvts.getRawParameterValue("follow_enabled")->load() > 0.5f;
    auto* sidechainBus = getBus(true, 0);
    bool hasSidechain = sidechainBus != nullptr && sidechainBus->isEnabled() && buffer.getNumChannels() >= 2;

    if (followEnabled && hasSidechain)
    {
        audioAnalyzer.processBlock(
            buffer.getReadPointer(0),
            buffer.getReadPointer(1),
            buffer.getNumSamples(),
            currentBPM,
            currentPositionBeats);
    }

    buffer.clear();
    if (!transportPlaying)
    {
        midiMessages.clear();
        return;
    }

    int styleIndex = static_cast<int>(apvts.getRawParameterValue("style")->load());
    int drummerIndex = static_cast<int>(apvts.getRawParameterValue("drummer")->load());
    int sectionIndex = static_cast<int>(apvts.getRawParameterValue("section")->load());
    int kitIndex = static_cast<int>(apvts.getRawParameterValue("kit")->load());

    float complexity = apvts.getRawParameterValue("complexity")->load();
    float loudness = apvts.getRawParameterValue("loudness")->load();
    float energy = apvts.getRawParameterValue("energy")->load();
    float groove = apvts.getRawParameterValue("groove")->load();
    float swing = apvts.getRawParameterValue("swing")->load();

    if (followEnabled && hasSidechain)
    {
        float followAmount = apvts.getRawParameterValue("follow_amount")->load();
        float followSensitivity = apvts.getRawParameterValue("follow_sensitivity")->load();
        audioAnalyzer.setSensitivity(followSensitivity);

        const auto& analysis = audioAnalyzer.getAnalysis();

        if (analysis.isActive && analysis.confidence > 0.3f && followAmount > 0.01f)
        {
            float blendFactor = analysis.confidence * followAmount;

            float targetComplexity = juce::jlimit(0.0f, 1.0f,
                complexity * 0.3f + analysis.onsetDensity * 0.7f);
            complexity = complexity * (1.0f - blendFactor) + targetComplexity * blendFactor;

            float targetLoudness = juce::jlimit(0.0f, 1.0f,
                loudness * 0.3f + analysis.smoothedEnergy * 0.7f);
            loudness = loudness * (1.0f - blendFactor) + targetLoudness * blendFactor;

            float inputEnergy = (analysis.smoothedEnergy + analysis.onsetDensity) * 0.5f;
            float targetEnergy = juce::jlimit(0.0f, 1.0f,
                energy * 0.3f + inputEnergy * 0.7f);
            energy = energy * (1.0f - blendFactor) + targetEnergy * blendFactor;

            if (analysis.suggestFill && followAmount > 0.3f)
            {
                float fillIntensity = juce::jlimit(0.3f, 1.0f, analysis.spectralFlux * 2.0f);
                drummerEngine.setFillIntensity(fillIntensity);
                drummerEngine.triggerFill(4);
            }
        }
    }

    drummerEngine.setStyle(styleIndex);
    drummerEngine.setDrummer(drummerIndex);
    drummerEngine.setSection(sectionIndex);
    drummerEngine.setKit(kitIndex);
    drummerEngine.setComplexity(complexity);
    drummerEngine.setLoudness(loudness);
    drummerEngine.setEnergy(energy);

    drummerEngine.process(buffer.getNumSamples(), currentBPM, currentPositionBeats, midiMessages);

    if (groove > 0.01f)
    {
        grooveHumanizer.setGrooveAmount(groove);
        grooveHumanizer.setSwing(swing);
        grooveHumanizer.process(midiMessages, currentBPM);
    }

}

//==============================================================================
bool GrooveMindProcessor::isFollowModeEnabled() const
{
    return apvts.getRawParameterValue("follow_enabled")->load() > 0.5f;
}

bool GrooveMindProcessor::isFollowModeActive() const
{
    return isFollowModeEnabled() && audioAnalyzer.getAnalysis().isActive;
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
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveMindProcessor();
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Helper function to create parameter layout
static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Core parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("complexity", 1), "Complexity",
        juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f), 5.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("loudness", 1), "Loudness",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 75.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("swing", 1), "Swing",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f));

    // Follow Mode parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("followEnabled", 1), "Follow Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("followSource", 1), "Follow Source",
        juce::StringArray{"MIDI", "Audio"}, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("followSensitivity", 1), "Follow Sensitivity",
        juce::NormalisableRange<float>(0.1f, 0.8f, 0.01f), 0.5f));

    // Style parameters
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("style", 1), "Style",
        juce::StringArray{"Rock", "HipHop", "Alternative", "R&B", "Electronic", "Trap", "Songwriter"}, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("drummer", 1), "Drummer",
        juce::StringArray{"Kyle - Rock", "Logan - Alternative", "Brooklyn - R&B", "Austin - HipHop"}, 0));

    // Fill parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("fillFrequency", 1), "Fill Frequency",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 30.0f));  // How often fills occur (%)

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("fillIntensity", 1), "Fill Intensity",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));  // How intense fills are

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("fillLength", 1), "Fill Length",
        juce::StringArray{"1 Beat", "2 Beats", "4 Beats"}, 0));  // Length of fills

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("fillTrigger", 1), "Trigger Fill", false));  // Manual fill trigger

    // Section arrangement parameter
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("section", 1), "Section",
        juce::StringArray{"Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Breakdown", "Outro"}, 1));  // Default to Verse

    // Advanced Humanization parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("humanTiming", 1), "Timing Variation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 20.0f));  // Timing randomization %

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("humanVelocity", 1), "Velocity Variation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 15.0f));  // Velocity randomization %

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("humanPush", 1), "Push/Drag Feel",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 1.0f), 0.0f));  // Ahead/behind the beat

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("humanGroove", 1), "Groove Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));  // How much groove template applies

    return { params.begin(), params.end() };
}

//==============================================================================
DrummerCloneAudioProcessor::DrummerCloneAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)  // Sidechain for bass/audio Follow Mode
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       parameters(*this, nullptr, juce::Identifier("DrummerCloneParameters"), createParameterLayout()),
       audioInputBuffer(2, 44100 * 2),  // 2 seconds stereo buffer at 44.1kHz
       drummerEngine(parameters)
{
    // Set up parameter listeners
    parameters.addParameterListener(PARAM_COMPLEXITY, this);
    parameters.addParameterListener(PARAM_LOUDNESS, this);
    parameters.addParameterListener(PARAM_SWING, this);
    parameters.addParameterListener(PARAM_FOLLOW_ENABLED, this);
    parameters.addParameterListener(PARAM_FOLLOW_SOURCE, this);
    parameters.addParameterListener(PARAM_FOLLOW_SENSITIVITY, this);
    parameters.addParameterListener(PARAM_STYLE, this);
    parameters.addParameterListener(PARAM_DRUMMER, this);

    // Start timer for UI updates (100ms)
    startTimer(100);
}

DrummerCloneAudioProcessor::~DrummerCloneAudioProcessor()
{
    stopTimer();
}

//==============================================================================
const juce::String DrummerCloneAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DrummerCloneAudioProcessor::acceptsMidi() const
{
    return true;
}

bool DrummerCloneAudioProcessor::producesMidi() const
{
    return true;
}

bool DrummerCloneAudioProcessor::isMidiEffect() const
{
    // Return false to enable sidechain audio input for Follow Mode
    return false;
}

double DrummerCloneAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DrummerCloneAudioProcessor::getNumPrograms()
{
    return 1;
}

int DrummerCloneAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DrummerCloneAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String DrummerCloneAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void DrummerCloneAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void DrummerCloneAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentSamplesPerBlock = samplesPerBlock;

    // Prepare buffers (stereo for sidechain input)
    audioInputBuffer.setSize(2, static_cast<int>(sampleRate * 2)); // 2 second stereo buffer
    audioInputBuffer.clear();

    // Prepare Follow Mode components
    transientDetector.prepare(sampleRate);
    midiGrooveExtractor.prepare(sampleRate);
    grooveTemplateGenerator.prepare(sampleRate);

    // Prepare drum engine
    drummerEngine.prepare(sampleRate, samplesPerBlock);
}

void DrummerCloneAudioProcessor::releaseResources()
{
    // Clean up resources
}

bool DrummerCloneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Support stereo sidechain input and stereo output
    // Also support mono configurations for compatibility
    auto inputSet = layouts.getMainInputChannelSet();
    auto outputSet = layouts.getMainOutputChannelSet();

    // Allow stereo or mono configurations
    if (inputSet != juce::AudioChannelSet::stereo() &&
        inputSet != juce::AudioChannelSet::mono() &&
        !inputSet.isDisabled())
        return false;

    if (outputSet != juce::AudioChannelSet::stereo() &&
        outputSet != juce::AudioChannelSet::mono())
        return false;

    return true;
}

void DrummerCloneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Get playhead information
    updatePlayheadInfo(getPlayHead());

    // Store incoming MIDI for Follow Mode
    if (followModeActive && !followSourceIsAudio)
    {
        for (const auto metadata : midiMessages)
            midiRingBuffer.push_back(metadata.getMessage());

        // Keep only last 2 seconds worth of MIDI
        auto currentTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
        midiRingBuffer.erase(
            std::remove_if(midiRingBuffer.begin(), midiRingBuffer.end(),
                [currentTime](const juce::MidiMessage& m) {
                    return (currentTime - m.getTimeStamp()) > 2.0;
                }),
            midiRingBuffer.end());
    }

    // Process Follow Mode if enabled
    if (followModeActive)
    {
        processFollowMode(buffer, midiMessages);
    }

    // Generate drum pattern if needed
    if (isPlaying && (needsRegeneration || isBarBoundary(ppqPosition, currentBPM)))
    {
        generateDrumPattern();
        needsRegeneration = false;
    }

    // Clear input MIDI and add our generated MIDI
    midiMessages.clear();

    if (!generatedMidiBuffer.isEmpty())
    {
        midiMessages.addEvents(generatedMidiBuffer, 0, buffer.getNumSamples(), 0);
    }

    // Pass through audio unchanged (we're a MIDI effect)
    // The audio is only used for Follow Mode analysis
}

void DrummerCloneAudioProcessor::updatePlayheadInfo(juce::AudioPlayHead* head)
{
    if (head != nullptr)
    {
        if (auto position = head->getPosition())
        {
            if (position->getBpm().hasValue())
                currentBPM = *position->getBpm();

            if (position->getPpqPosition().hasValue())
                ppqPosition = *position->getPpqPosition();

            isPlaying = position->getIsPlaying();
        }
    }
}

void DrummerCloneAudioProcessor::processFollowMode(const juce::AudioBuffer<float>& buffer,
                                                   const juce::MidiBuffer& midi)
{
    if (followSourceIsAudio)
    {
        // Analyze audio for transients
        auto detectedOnsets = transientDetector.process(buffer);

        if (!detectedOnsets.empty())
        {
            // Generate groove template from audio transients
            currentGroove = grooveTemplateGenerator.generateFromOnsets(
                detectedOnsets, currentBPM, currentSampleRate);

            grooveFollower.update(currentGroove);
            grooveLockPercentage = grooveFollower.getLockPercentage();
        }
    }
    else
    {
        // Analyze MIDI for groove
        auto extractedGroove = midiGrooveExtractor.extractFromBuffer(midi);

        if (extractedGroove.noteCount > 0)
        {
            currentGroove = grooveTemplateGenerator.generateFromMidi(
                extractedGroove, currentBPM);

            grooveFollower.update(currentGroove);
            grooveLockPercentage = grooveFollower.getLockPercentage();
        }
    }
}

void DrummerCloneAudioProcessor::generateDrumPattern()
{
    // Get current bar number
    int currentBar = static_cast<int>(ppqPosition / 4.0);

    // Only regenerate if we're at a new bar
    if (currentBar != lastGeneratedBar)
    {
        // Get parameters
        auto complexity = parameters.getRawParameterValue(PARAM_COMPLEXITY)->load();
        auto loudness = parameters.getRawParameterValue(PARAM_LOUDNESS)->load();
        auto swing = parameters.getRawParameterValue(PARAM_SWING)->load();
        auto style = parameters.getParameter(PARAM_STYLE)->getValue();

        // Apply Follow Mode groove if active
        GrooveTemplate grooveToUse = followModeActive ?
            grooveFollower.getCurrent(ppqPosition) : GrooveTemplate();

        // Generate pattern
        generatedMidiBuffer = drummerEngine.generateRegion(
            1,  // Generate 1 bar at a time
            currentBPM,
            static_cast<int>(style * 7),  // Convert to style index
            grooveToUse,
            complexity,
            loudness,
            swing
        );

        lastGeneratedBar = currentBar;
    }
}

bool DrummerCloneAudioProcessor::isBarBoundary(double ppq, double bpm)
{
    juce::ignoreUnused(bpm);
    // Check if we're within a small window of a bar boundary
    double barLength = 4.0;  // 4 beats per bar
    double position = std::fmod(ppq, barLength);
    return position < 0.01 || position > (barLength - 0.01);
}

void DrummerCloneAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == PARAM_FOLLOW_ENABLED)
    {
        followModeActive = newValue > 0.5f;
    }
    else if (parameterID == PARAM_FOLLOW_SOURCE)
    {
        followSourceIsAudio = static_cast<int>(newValue) == 1;
    }
    else if (parameterID == PARAM_FOLLOW_SENSITIVITY)
    {
        followSensitivity = newValue;
        transientDetector.setSensitivity(newValue);
    }
    else
    {
        // Any other parameter change triggers regeneration
        needsRegeneration = true;
    }
}

void DrummerCloneAudioProcessor::timerCallback()
{
    // This timer is for UI updates
    // The editor will poll for current state
}

//==============================================================================
bool DrummerCloneAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* DrummerCloneAudioProcessor::createEditor()
{
    return new DrummerCloneAudioProcessorEditor (*this);
}

//==============================================================================
void DrummerCloneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Store parameters
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void DrummerCloneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore parameters
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName(parameters.state.getType()))
        {
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrummerCloneAudioProcessor();
}
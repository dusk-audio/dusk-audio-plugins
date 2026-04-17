#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ChordAnalyzerProcessor::ChordAnalyzerProcessor()
    : AudioProcessor(
#if CHORD_ANALYZER_MIDI_MODE
        BusesProperties()
#else
        BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
      ),
      parameters(*this, nullptr, juce::Identifier("ChordAnalyzerState"),
                 createParameterLayout())
{
    // Register as listener for parameter changes
    parameters.addParameterListener(PARAM_KEY_ROOT, this);
    parameters.addParameterListener(PARAM_KEY_MODE, this);
    parameters.addParameterListener(PARAM_SUGGESTION_LEVEL, this);
    parameters.addParameterListener(PARAM_SHOW_INVERSIONS, this);

    // Initialize from current parameter values
    keyRoot.store(static_cast<int>(*parameters.getRawParameterValue(PARAM_KEY_ROOT)));
    keyMinor.store(*parameters.getRawParameterValue(PARAM_KEY_MODE) > 0.5f);
    suggestionLevel.store(static_cast<int>(*parameters.getRawParameterValue(PARAM_SUGGESTION_LEVEL)));
    showInversions.store(*parameters.getRawParameterValue(PARAM_SHOW_INVERSIONS) > 0.5f);

    analyzer.setKey(keyRoot.load(), keyMinor.load());

    // Output parameters (detection results) — added directly to the processor
    // rather than via APVTS. APVTS's ValueTree sync was silently overwriting
    // setValueNotifyingHost writes, so detected values never reached the host.
    // Using ASCII "-" for the "no chord" label: Reaper's parameter strip renders
    // multibyte UTF-8 (em-dash) as Latin-1 mojibake.
    const juce::StringArray pitchClassChoices{
        "-", "C", "C#/Db", "D", "D#/Eb", "E", "F",
        "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };

    // Quality choices: index 0 = "-" (no chord). Indices 1..N follow ChordQuality
    // enum order — keep these in sync if the enum is ever reordered.
    const juce::StringArray qualityChoices{
        "-",
        "maj", "m", "dim", "aug",
        "7", "maj7", "m7", "mMaj7", "dim7", "m7b5", "aug7", "augMaj7",
        "sus2", "sus4", "7sus4",
        "add9", "add11",
        "6", "m6",
        "maj9", "m9", "9",
        "maj11", "m11", "11",
        "maj13", "m13", "13",
        "5",
        "7b5", "7#5", "7b9", "7#9" };

    const juce::StringArray inversionChoices{ "-", "Root", "1st", "2nd", "3rd" };

    detectedRootParam = new juce::AudioParameterChoice(
        juce::ParameterID(PARAM_DETECTED_ROOT, 1),
        "Detected Root", pitchClassChoices, 0);
    detectedQualityParam = new juce::AudioParameterChoice(
        juce::ParameterID(PARAM_DETECTED_QUALITY, 1),
        "Detected Quality", qualityChoices, 0);
    detectedBassParam = new juce::AudioParameterChoice(
        juce::ParameterID(PARAM_DETECTED_BASS, 1),
        "Detected Bass", pitchClassChoices, 0);
    detectedInversionParam = new juce::AudioParameterChoice(
        juce::ParameterID(PARAM_DETECTED_INVERSION, 1),
        "Detected Inversion", inversionChoices, 0);

    addParameter(detectedRootParam);
    addParameter(detectedQualityParam);
    addParameter(detectedBassParam);
    addParameter(detectedInversionParam);

    startTimer(50);  // 20Hz publishing of detection results to host parameters
}

ChordAnalyzerProcessor::~ChordAnalyzerProcessor()
{
    parameters.removeParameterListener(PARAM_KEY_ROOT, this);
    parameters.removeParameterListener(PARAM_KEY_MODE, this);
    parameters.removeParameterListener(PARAM_SUGGESTION_LEVEL, this);
    parameters.removeParameterListener(PARAM_SHOW_INVERSIONS, this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ChordAnalyzerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Key root selection (C=0 through B=11)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(PARAM_KEY_ROOT, 1),
        "Key Root",
        juce::StringArray{"C", "C#/Db", "D", "D#/Eb", "E", "F",
                          "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"},
        0));  // Default to C

    // Key mode (Major/Minor)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(PARAM_KEY_MODE, 1),
        "Key Mode",
        juce::StringArray{"Major", "Minor"},
        0));  // Default to Major

    // Suggestion level (how many suggestion tiers to show)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(PARAM_SUGGESTION_LEVEL, 1),
        "Suggestion Level",
        juce::StringArray{"Basic Only", "Basic + Intermediate", "All (+ Advanced)"},
        2));  // Default to All

    // Show inversions
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(PARAM_SHOW_INVERSIONS, 1),
        "Show Inversions",
        true));  // Default to showing inversions

    // NOTE: detection-output parameters are added directly to the processor
    // (not APVTS-managed) in the constructor body — see ctor for rationale.

    return {params.begin(), params.end()};
}

//==============================================================================
void ChordAnalyzerProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == PARAM_KEY_ROOT)
    {
        int newRoot = static_cast<int>(newValue);
        keyRoot.store(newRoot);
        analyzer.setKey(newRoot, keyMinor.load());

        // Re-analyze with new key
        updateAnalysis();
    }
    else if (parameterID == PARAM_KEY_MODE)
    {
        bool isMinor = newValue > 0.5f;
        keyMinor.store(isMinor);
        analyzer.setKey(keyRoot.load(), isMinor);

        // Re-analyze with new key
        updateAnalysis();
    }
    else if (parameterID == PARAM_SUGGESTION_LEVEL)
    {
        suggestionLevel.store(static_cast<int>(newValue));
        updateAnalysis();  // Refresh suggestions
    }
    else if (parameterID == PARAM_SHOW_INVERSIONS)
    {
        showInversions.store(newValue > 0.5f);
    }
}

//==============================================================================
const juce::String ChordAnalyzerProcessor::getName() const
{
    return JucePlugin_Name;
}

int ChordAnalyzerProcessor::getNumPrograms()
{
    return 1;
}

int ChordAnalyzerProcessor::getCurrentProgram()
{
    return 0;
}

void ChordAnalyzerProcessor::setCurrentProgram(int /*index*/)
{
}

const juce::String ChordAnalyzerProcessor::getProgramName(int /*index*/)
{
    return {};
}

void ChordAnalyzerProcessor::changeProgramName(int /*index*/, const juce::String& /*newName*/)
{
}

//==============================================================================
void ChordAnalyzerProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    currentTimeSec = 0.0;
    lastAnalysisTime = 0.0;
}

void ChordAnalyzerProcessor::releaseResources()
{
    // Clear active notes when stopping
    {
        const juce::SpinLock::ScopedLockType lock(notesLock);
        activeNotes.clear();
    }
}

//==============================================================================
void ChordAnalyzerProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
#if CHORD_ANALYZER_MIDI_MODE
    // MIDI-only mode: no audio buses (buffer is empty)
    juce::ignoreUnused(buffer);
#else
    // Instrument mode (synth): produce silence
    buffer.clear();
#endif

    // Process MIDI input
    processMidiInput(midiMessages);

    // Update timing
    double blockDuration = buffer.getNumSamples() / currentSampleRate;
    currentTimeSec += blockDuration;

    // Debounced analysis (50ms)
    if (currentTimeSec - lastAnalysisTime >= analysisIntervalSec)
    {
        updateAnalysis();
        lastAnalysisTime = currentTimeSec;
    }
}

//==============================================================================
void ChordAnalyzerProcessor::processMidiInput(const juce::MidiBuffer& midi)
{
    bool notesChanged = false;

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            int noteNumber = msg.getNoteNumber();
            const juce::SpinLock::ScopedLockType lock(notesLock);

            // Add note if not already present
            if (std::find(activeNotes.begin(), activeNotes.end(), noteNumber) == activeNotes.end())
            {
                activeNotes.push_back(noteNumber);
                notesChanged = true;
            }
        }
        else if (msg.isNoteOff())
        {
            int noteNumber = msg.getNoteNumber();
            const juce::SpinLock::ScopedLockType lock(notesLock);

            // Remove note
            auto it = std::find(activeNotes.begin(), activeNotes.end(), noteNumber);
            if (it != activeNotes.end())
            {
                activeNotes.erase(it);
                notesChanged = true;
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            const juce::SpinLock::ScopedLockType lock(notesLock);
            activeNotes.clear();
            notesChanged = true;
        }
    }

    // Trigger immediate analysis if notes changed significantly
    if (notesChanged)
    {
        updateAnalysis();
        lastAnalysisTime = currentTimeSec;
    }
}

//==============================================================================
void ChordAnalyzerProcessor::updateAnalysis()
{
    std::vector<int> notesCopy;
    {
        const juce::SpinLock::ScopedLockType lock(notesLock);
        notesCopy = activeNotes;
    }

    // Analyze the current notes
    ChordInfo newChord = analyzer.analyze(notesCopy);

    // Get suggestions based on suggestion level
    SuggestionCategory maxLevel;
    switch (suggestionLevel.load())
    {
        case 0: maxLevel = SuggestionCategory::Basic; break;
        case 1: maxLevel = SuggestionCategory::Intermediate; break;
        default: maxLevel = SuggestionCategory::Advanced; break;
    }
    std::vector<ChordSuggestion> newSuggestions = analyzer.getSuggestions(newChord, maxLevel);

    // Update thread-safe chord state
    {
        const juce::SpinLock::ScopedLockType lock(chordLock);

        if (newChord != currentChord)
        {
            currentChord = newChord;
            chordChangedFlag.store(true);

            stageDetectedChord(newChord);

            // Record the chord if recording
            const juce::SpinLock::ScopedLockType recLock(recorderLock);
            if (recorder.isRecording())
            {
                recorder.recordChord(newChord, currentTimeSec);
            }
        }

        currentSuggestions = std::move(newSuggestions);
    }
}

//==============================================================================
void ChordAnalyzerProcessor::stageDetectedChord(const ChordInfo& chord)
{
    // Audio thread: stage detection results into atomics. The Timer publishes
    // them on the message thread (where setValueNotifyingHost is host-safe).
    // Choice index 0 always represents "no chord / unknown".
    const int rootIndex      = (chord.isValid && chord.rootNote >= 0 && chord.rootNote < 12)
                                  ? chord.rootNote + 1 : 0;
    const int bassIndex      = (chord.isValid && chord.bassNote >= 0 && chord.bassNote < 12)
                                  ? chord.bassNote + 1 : 0;
    const int qualityIndex   = (chord.isValid && chord.quality != ChordQuality::Unknown)
                                  ? static_cast<int>(chord.quality) + 1 : 0;
    const int inversionIndex = chord.isValid
                                  ? juce::jlimit(0, 4, chord.inversion + 1) : 0;

    pendingRootIndex.store(rootIndex);
    pendingQualityIndex.store(qualityIndex);
    pendingBassIndex.store(bassIndex);
    pendingInversionIndex.store(inversionIndex);
    detectionDirty.store(true);
}

void ChordAnalyzerProcessor::timerCallback()
{
    if (! detectionDirty.exchange(false))
        return;

    if (detectedRootParam      != nullptr) *detectedRootParam      = pendingRootIndex.load();
    if (detectedQualityParam   != nullptr) *detectedQualityParam   = pendingQualityIndex.load();
    if (detectedBassParam      != nullptr) *detectedBassParam      = pendingBassIndex.load();
    if (detectedInversionParam != nullptr) *detectedInversionParam = pendingInversionIndex.load();
}

//==============================================================================
ChordInfo ChordAnalyzerProcessor::getCurrentChord() const
{
    const juce::SpinLock::ScopedLockType lock(chordLock);
    return currentChord;
}

bool ChordAnalyzerProcessor::hasChordChanged()
{
    return chordChangedFlag.exchange(false);
}

std::vector<ChordSuggestion> ChordAnalyzerProcessor::getCurrentSuggestions() const
{
    const juce::SpinLock::ScopedLockType lock(chordLock);
    return currentSuggestions;
}

std::vector<int> ChordAnalyzerProcessor::getActiveNotes() const
{
    const juce::SpinLock::ScopedLockType lock(notesLock);
    return activeNotes;
}

juce::String ChordAnalyzerProcessor::getKeyName() const
{
    return ChordAnalyzer::pitchClassToName(keyRoot.load()) +
           (keyMinor.load() ? " Minor" : " Major");
}

//==============================================================================
// Recording controls
void ChordAnalyzerProcessor::startRecording()
{
    const juce::SpinLock::ScopedLockType lock(recorderLock);
    recorder.setKey(keyRoot.load(), keyMinor.load());
    recorder.startRecording();
}

void ChordAnalyzerProcessor::stopRecording()
{
    const juce::SpinLock::ScopedLockType lock(recorderLock);
    recorder.stopRecording();
}

bool ChordAnalyzerProcessor::isRecording() const
{
    const juce::SpinLock::ScopedLockType lock(recorderLock);
    return recorder.isRecording();
}

void ChordAnalyzerProcessor::clearRecording()
{
    const juce::SpinLock::ScopedLockType lock(recorderLock);
    recorder.clearSession();
}

juce::String ChordAnalyzerProcessor::exportRecordingToJSON() const
{
    const juce::SpinLock::ScopedLockType lock(recorderLock);
    return recorder.exportToJSON();
}

int ChordAnalyzerProcessor::getRecordedEventCount() const
{
    const juce::SpinLock::ScopedLockType lock(recorderLock);
    return recorder.getEventCount();
}

//==============================================================================
void ChordAnalyzerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ChordAnalyzerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
    {
        if (xmlState->hasTagName(parameters.state.getType()))
        {
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* ChordAnalyzerProcessor::createEditor()
{
    return new ChordAnalyzerEditor(*this);
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChordAnalyzerProcessor();
}

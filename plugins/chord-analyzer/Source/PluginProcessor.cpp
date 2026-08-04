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
    parameters.addParameterListener(PARAM_RESPECT_SUSTAIN, this);

    // Initialize from current parameter values
    keyRoot.store(static_cast<int>(*parameters.getRawParameterValue(PARAM_KEY_ROOT)));
    keyMinor.store(*parameters.getRawParameterValue(PARAM_KEY_MODE) > 0.5f);
    suggestionLevel.store(static_cast<int>(*parameters.getRawParameterValue(PARAM_SUGGESTION_LEVEL)));
    showInversions.store(*parameters.getRawParameterValue(PARAM_SHOW_INVERSIONS) > 0.5f);
    respectSustain.store(*parameters.getRawParameterValue(PARAM_RESPECT_SUSTAIN) > 0.5f);

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
    static constexpr const char* qualityChoiceLabels[]{
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
    constexpr int qualityChoiceCount =
        static_cast<int>(sizeof(qualityChoiceLabels) / sizeof(qualityChoiceLabels[0]));
    static_assert(qualityChoiceCount
                      == static_cast<int>(ChordQuality::Unknown) + 1,
                  "Detected-quality choices must match ChordQuality enum order");
    const juce::StringArray qualityChoices(
        qualityChoiceLabels, qualityChoiceCount);

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
    parameters.removeParameterListener(PARAM_RESPECT_SUSTAIN, this);
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

    // Respect sustain pedal — when on, MIDI CC 64 holds the detected chord
    // until the pedal is released (matches piano hardware behaviour, useful
    // for transcription workflows where players want to lift their hands).
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(PARAM_RESPECT_SUSTAIN, 1),
        "Respect Sustain",
        true));

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
        analysisDirty.store(true, std::memory_order_release);
    }
    else if (parameterID == PARAM_KEY_MODE)
    {
        bool isMinor = newValue > 0.5f;
        keyMinor.store(isMinor);
        analysisDirty.store(true, std::memory_order_release);
    }
    else if (parameterID == PARAM_SUGGESTION_LEVEL)
    {
        suggestionLevel.store(static_cast<int>(newValue));
        analysisDirty.store(true, std::memory_order_release);
    }
    else if (parameterID == PARAM_SHOW_INVERSIONS)
    {
        showInversions.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_RESPECT_SUSTAIN)
    {
        respectSustain.store(newValue > 0.5f);
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
void ChordAnalyzerProcessor::resetNoteState() noexcept
{
    sustainPedalDown = false;
    for (auto& word : activeNoteWords)
        word.store(0, std::memory_order_release);
    sustainedReleasedWords.fill(0);
    analysisQueueRead.store(
        analysisQueueWrite.load(std::memory_order_acquire),
        std::memory_order_release);
    analysisDirty.store(true, std::memory_order_release);
}

void ChordAnalyzerProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    currentTimeSec.store(0.0, std::memory_order_relaxed);
    // The timeline restarts at 0, so any note or pedal state carried over from
    // a previous run would be timestamped against a clock that no longer exists.
    resetNoteState();
}

void ChordAnalyzerProcessor::releaseResources()
{
    resetNoteState();
}

//==============================================================================
void ChordAnalyzerProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0)
        return;

#if CHORD_ANALYZER_MIDI_MODE
    // MIDI-only mode: no audio buses (buffer is empty)
    juce::ignoreUnused(buffer);
#else
    // Instrument mode (synth): produce silence
    buffer.clear();
#endif

    // Sample the host tempo so startRecording() can apply it to the
    // recorder. Cheap and safe to do every block. If the host exposes a
    // Position but no BPM (offline render, some MIDI-only hosts), fall
    // back to 120 — without this we'd keep a stale value from whichever
    // host last reported a tempo.
    if (auto* head = getPlayHead())
    {
        if (auto pos = head->getPosition())
        {
            if (auto bpm = pos->getBpm())
                hostBPM.store(*bpm, std::memory_order_relaxed);
            else
                hostBPM.store(120.0, std::memory_order_relaxed);
        }
    }

    // Process MIDI input
    processMidiInput(midiMessages);

    // Update timing
    double blockDuration = buffer.getNumSamples() / currentSampleRate;
    const double now = currentTimeSec.load(std::memory_order_relaxed) + blockDuration;
    currentTimeSec.store(now, std::memory_order_relaxed);

}

//==============================================================================
bool ChordAnalyzerProcessor::activateNote(int note) noexcept
{
    if (note < 0 || note >= 128)
        return false;
    const size_t word = static_cast<size_t>(note / 64);
    const uint64_t mask = uint64_t{1} << static_cast<unsigned>(note % 64);
    const uint64_t previous =
        activeNoteWords[word].fetch_or(mask, std::memory_order_acq_rel);
    return (previous & mask) == 0;
}

bool ChordAnalyzerProcessor::deactivateNote(int note) noexcept
{
    if (note < 0 || note >= 128)
        return false;
    const size_t word = static_cast<size_t>(note / 64);
    const uint64_t mask = uint64_t{1} << static_cast<unsigned>(note % 64);
    const uint64_t previous =
        activeNoteWords[word].fetch_and(~mask, std::memory_order_acq_rel);
    return (previous & mask) != 0;
}

std::array<uint64_t, 2>
ChordAnalyzerProcessor::snapshotActiveNoteWords() const noexcept
{
    return {
        activeNoteWords[0].load(std::memory_order_acquire),
        activeNoteWords[1].load(std::memory_order_acquire) };
}

std::vector<int> ChordAnalyzerProcessor::notesFromWords(
    const std::array<uint64_t, 2>& words)
{
    std::vector<int> notes;
    notes.reserve(16);
    for (int note = 0; note < 128; ++note)
        if ((words[static_cast<size_t>(note / 64)]
             & (uint64_t{1} << static_cast<unsigned>(note % 64))) != 0)
            notes.push_back(note);
    return notes;
}

std::vector<int> ChordAnalyzerProcessor::snapshotActiveNotes() const
{
    return notesFromWords(snapshotActiveNoteWords());
}

bool ChordAnalyzerProcessor::enqueueAnalysisSnapshot(double timeSec) noexcept
{
    const size_t write = analysisQueueWrite.load(std::memory_order_relaxed);
    const size_t next = (write + 1) % analysisQueueCapacity;
    if (next == analysisQueueRead.load(std::memory_order_acquire))
        return false;
    analysisQueue[write] = { snapshotActiveNoteWords(), timeSec };
    analysisQueueWrite.store(next, std::memory_order_release);
    return true;
}

bool ChordAnalyzerProcessor::dequeueAnalysisSnapshot(
    PendingAnalysis& snapshot) noexcept
{
    const size_t read = analysisQueueRead.load(std::memory_order_relaxed);
    if (read == analysisQueueWrite.load(std::memory_order_acquire))
        return false;
    snapshot = analysisQueue[read];
    analysisQueueRead.store(
        (read + 1) % analysisQueueCapacity, std::memory_order_release);
    return true;
}

//==============================================================================
void ChordAnalyzerProcessor::processMidiInput(const juce::MidiBuffer& midi)
{
    bool notesChanged = false;
    const bool sustainEnabled = respectSustain.load();

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            const int noteNumber = msg.getNoteNumber();
            // Re-pressing a sustained-released note cancels its deferred release
            const size_t word = static_cast<size_t>(noteNumber / 64);
            const uint64_t mask =
                uint64_t{1} << static_cast<unsigned>(noteNumber % 64);
            sustainedReleasedWords[word] &= ~mask;
            notesChanged = activateNote(noteNumber) || notesChanged;
        }
        else if (msg.isNoteOff())
        {
            const int noteNumber = msg.getNoteNumber();

            if (sustainEnabled && sustainPedalDown)
            {
                // Defer the release: keep the active bit set until the pedal lifts.
                const size_t word = static_cast<size_t>(noteNumber / 64);
                const uint64_t mask =
                    uint64_t{1} << static_cast<unsigned>(noteNumber % 64);
                sustainedReleasedWords[word] |= mask;
                continue;
            }

            notesChanged = deactivateNote(noteNumber) || notesChanged;
        }
        else if (msg.isController() && msg.getControllerNumber() == 64)
        {
            // Sustain pedal: MIDI convention is value >= 64 → down, < 64 → up
            const bool wasDown   = sustainPedalDown;
            const bool nowDown   = msg.getControllerValue() >= 64;
            sustainPedalDown     = nowDown;

            if (wasDown && ! nowDown)
            {
                // Pedal release: drop every note whose note-off was deferred.
                // We always flush regardless of the current sustainEnabled state —
                // those bits reflect real player-released note-offs from when
                // sustain was on, and would otherwise stay stuck active
                // if the user toggled "Respect Sustain" off while pedalling.
                for (size_t word = 0; word < sustainedReleasedWords.size(); ++word)
                {
                    const uint64_t released = sustainedReleasedWords[word];
                    const uint64_t previous = activeNoteWords[word].fetch_and(
                        ~released, std::memory_order_acq_rel);
                    notesChanged = (previous & released) != 0 || notesChanged;
                    sustainedReleasedWords[word] = 0;
                }
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& word : activeNoteWords)
                notesChanged = word.exchange(0, std::memory_order_acq_rel) != 0
                            || notesChanged;
            sustainedReleasedWords.fill(0);
        }
    }

    // Chord naming and suggestion generation build strings/vectors, so queue
    // the note state for the existing 20 Hz message-thread timer. The snapshot
    // is taken once per block, after the whole MIDI buffer has been applied, so
    // it preserves chords that span block boundaries but arrive between timer
    // ticks. A chord that both begins and ends inside a single processBlock is
    // NOT preserved: only the resulting end-of-block state is captured.
    if (notesChanged)
    {
        if (! enqueueAnalysisSnapshot(currentTimeSec.load(std::memory_order_relaxed)))
            analysisDirty.store(true, std::memory_order_release);
    }
}

//==============================================================================
void ChordAnalyzerProcessor::updateAnalysis()
{
    updateAnalysis({ snapshotActiveNoteWords(),
                     currentTimeSec.load(std::memory_order_relaxed) });
}

void ChordAnalyzerProcessor::updateAnalysis(const PendingAnalysis& snapshot)
{
    std::vector<int> notesCopy = notesFromWords(snapshot.noteWords);

    analyzer.setKey(keyRoot.load(), keyMinor.load());

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

            // Always-on history — append valid chords so the editor can show
            // the last N played even after notes are released. Writes go
            // into a preallocated ring buffer so updates never need to shift
            // entries (the previous std::vector
            // push_back / erase(begin()) was both heap-touching and O(N)).
            if (newChord.isValid && !newChord.name.isEmpty() && newChord.name != "-")
            {
                const int lastIdx = (historyHead - 1 + maxHistorySize) % maxHistorySize;
                const bool isDup = historyCount > 0 && chordHistory[lastIdx] == newChord;
                if (!isDup)
                {
                    chordHistory[historyHead] = newChord;
                    historyHead = (historyHead + 1) % maxHistorySize;
                    if (historyCount < maxHistorySize)
                        ++historyCount;
                }
            }

            // Record the chord if recording
            const juce::SpinLock::ScopedLockType recLock(recorderLock);
            if (recorder.isRecording())
            {
                recorder.recordChord(newChord, snapshot.timeSec);
            }
        }

        currentSuggestions = std::move(newSuggestions);
    }
}

//==============================================================================
void ChordAnalyzerProcessor::stageDetectedChord(const ChordInfo& chord)
{
    // Stage detection results into atomics. The timer publishes them through
    // setValueNotifyingHost after analysis completes.
    // Choice index 0 always represents "no chord / unknown".
    const int rootIndex      = (chord.isValid && chord.rootNote >= 0 && chord.rootNote < 12)
                                  ? chord.rootNote + 1 : 0;
    const int bassIndex      = (chord.isValid && chord.bassNote >= 0 && chord.bassNote < 12)
                                  ? chord.bassNote + 1 : 0;
    const int rawQualityIndex =
        (chord.isValid && chord.quality != ChordQuality::Unknown)
            ? static_cast<int>(chord.quality) + 1 : 0;
    const int maxQualityIndex = detectedQualityParam != nullptr
        ? std::max(0, detectedQualityParam->choices.size() - 1) : 0;
    const int qualityIndex = juce::jlimit(
        0, maxQualityIndex, rawQualityIndex);
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
    PendingAnalysis snapshot;
    while (dequeueAnalysisSnapshot(snapshot))
        updateAnalysis(snapshot);

    if (analysisDirty.exchange(false, std::memory_order_acq_rel))
        updateAnalysis();

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

std::vector<ChordInfo> ChordAnalyzerProcessor::getChordHistory() const
{
    const juce::SpinLock::ScopedLockType lock(chordLock);
    std::vector<ChordInfo> out;
    out.reserve(static_cast<size_t>(historyCount));
    const int start = (historyHead - historyCount + maxHistorySize) % maxHistorySize;
    for (int i = 0; i < historyCount; ++i)
        out.push_back(chordHistory[(start + i) % maxHistorySize]);
    return out;
}

void ChordAnalyzerProcessor::clearChordHistory()
{
    const juce::SpinLock::ScopedLockType lock(chordLock);
    historyHead = 0;
    historyCount = 0;
}

std::vector<int> ChordAnalyzerProcessor::getActiveNotes() const
{
    return snapshotActiveNotes();
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
    // Order matters: startRecording() calls clearSession() which resets
    // the session's tempo and key fields, so apply them afterwards.
    recorder.startRecording();
    recorder.setKey(keyRoot.load(), keyMinor.load());
    recorder.setTempo(hostBPM.load(std::memory_order_relaxed));
}

void ChordAnalyzerProcessor::stopRecording()
{
    const juce::SpinLock::ScopedLockType lock(recorderLock);
    recorder.stopRecording(currentTimeSec.load(std::memory_order_relaxed));
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

int ChordAnalyzerProcessor::getRecordedEventCount() const
{
    const juce::SpinLock::ScopedLockType lock(recorderLock);
    return recorder.getEventCount();
}

RecordingSession ChordAnalyzerProcessor::getRecordingSession() const
{
    const juce::SpinLock::ScopedLockType lock(recorderLock);
    return recorder.getSession();
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

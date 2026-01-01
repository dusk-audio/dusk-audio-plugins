/*
  ==============================================================================

    DrummerEngine.cpp
    Core drummer engine implementation

  ==============================================================================
*/

#include "DrummerEngine.h"

//==============================================================================
DrummerEngine::DrummerEngine(PatternLibrary& library)
    : patternLibrary(library)
{
}

//==============================================================================
void DrummerEngine::prepare(double newSampleRate, int /*samplesPerBlock*/)
{
    sampleRate = newSampleRate;
}

//==============================================================================
void DrummerEngine::setStyle(int styleIndex)
{
    auto newStyle = static_cast<DrummerStyle>(juce::jlimit(0, static_cast<int>(DrummerStyle::NumStyles) - 1, styleIndex));
    if (newStyle != currentStyle)
    {
        currentStyle = newStyle;
        currentPattern = nullptr;  // Force pattern reselection
    }
}

void DrummerEngine::setDrummer(int drummerIndex)
{
    currentDrummer = static_cast<DrummerPersonality>(
        juce::jlimit(0, static_cast<int>(DrummerPersonality::NumDrummers) - 1, drummerIndex));
}

void DrummerEngine::setSection(int sectionIndex)
{
    auto newSection = static_cast<SongSection>(
        juce::jlimit(0, static_cast<int>(SongSection::NumSections) - 1, sectionIndex));
    if (newSection != currentSection)
    {
        currentSection = newSection;
        currentPattern = nullptr;  // Force pattern reselection
    }
}

void DrummerEngine::setKit(int kitIndex)
{
    auto newKit = static_cast<KitType>(
        juce::jlimit(0, static_cast<int>(KitType::NumKits) - 1, kitIndex));
    if (newKit != currentKit)
    {
        currentKit = newKit;
        currentPattern = nullptr;  // Force pattern reselection
    }
}

void DrummerEngine::setComplexity(float value)
{
    complexity = juce::jlimit(0.0f, 1.0f, value);
}

void DrummerEngine::setLoudness(float value)
{
    loudness = juce::jlimit(0.0f, 1.0f, value);
}

void DrummerEngine::setEnergy(float value)
{
    float newEnergy = juce::jlimit(0.0f, 1.0f, value);
    if (std::abs(newEnergy - energy) > 0.2f)
    {
        energy = newEnergy;
        // Consider switching patterns on significant energy change
    }
    else
    {
        energy = newEnergy;
    }
}

void DrummerEngine::setFillMode(int mode)
{
    fillMode = juce::jlimit(0, 2, mode);
}

void DrummerEngine::setFillIntensity(float value)
{
    fillIntensity = juce::jlimit(0.0f, 1.0f, value);
}

void DrummerEngine::triggerFill(int lengthInBeats)
{
    fillRequested = true;
    fillLengthBeats = lengthInBeats;
}

//==============================================================================
PatternQuery DrummerEngine::buildQuery() const
{
    PatternQuery query;

    // Map style enum to string
    static const char* styleNames[] = {
        "rock", "pop", "funk", "soul", "jazz", "blues",
        "hiphop", "rnb", "electronic", "latin", "country", "punk"
    };
    query.style = styleNames[static_cast<int>(currentStyle)];

    // Kit type
    static const char* kitNames[] = { "acoustic", "brush", "electronic", "hybrid" };
    query.kit = kitNames[static_cast<int>(currentKit)];

    // Section
    static const char* sectionNames[] = {
        "intro", "verse", "pre-chorus", "chorus", "bridge", "breakdown", "outro"
    };
    query.section = sectionNames[static_cast<int>(currentSection)];

    query.type = "beat";
    query.targetEnergy = energy;
    query.targetComplexity = complexity;
    query.requireBrushSweeps = (currentKit == KitType::Brush);

    return query;
}

//==============================================================================
void DrummerEngine::selectNewPattern()
{
    auto query = buildQuery();
    currentPattern = patternLibrary.selectPattern(query);

    if (currentPattern != nullptr)
    {
        DBG("DrummerEngine: Selected pattern " + currentPattern->metadata.id +
            " (energy=" + juce::String(currentPattern->metadata.energy) +
            ", complexity=" + juce::String(currentPattern->metadata.complexity) + ")");
    }
}

void DrummerEngine::selectFillPattern()
{
    auto query = buildQuery();
    query.targetEnergy = energy * (0.8f + fillIntensity * 0.4f);  // Fills can be more energetic

    currentFillPattern = patternLibrary.selectFill(query, fillLengthBeats);

    if (currentFillPattern != nullptr)
    {
        DBG("DrummerEngine: Selected fill " + currentFillPattern->metadata.id);
    }
}

//==============================================================================
bool DrummerEngine::shouldAutoFill(double positionInBeats) const
{
    if (fillMode != 0)  // Not auto mode
        return false;

    // Cooldown: don't trigger another fill within 8 beats of the last one ending
    if (positionInBeats < lastFillEndBeat + 8.0)
        return false;

    // Trigger fills at phrase boundaries (every 8 bars for verse, 4 for chorus)
    int barsPerPhrase = (currentSection == SongSection::Chorus ||
                         currentSection == SongSection::Breakdown) ? 4 : 8;
    int beatsPerPhrase = barsPerPhrase * 4;

    double positionInPhrase = std::fmod(positionInBeats, beatsPerPhrase);

    // Fill at last bar of phrase (only trigger once at the start of the fill window)
    return positionInPhrase >= (beatsPerPhrase - 4) &&
           positionInPhrase < (beatsPerPhrase - 4 + 0.1);
}

//==============================================================================
bool DrummerEngine::shouldPlayNote(int midiNote) const
{
    // Filter based on instrument enables
    if (!kickEnabled && midiNote >= KICK_NOTE_MIN && midiNote <= KICK_NOTE_MAX)
        return false;
    if (!snareEnabled && midiNote >= SNARE_NOTE_MIN && midiNote <= SNARE_NOTE_MAX)
        return false;
    if (!hihatEnabled && midiNote >= HIHAT_NOTE_MIN && midiNote <= HIHAT_NOTE_MAX)
        return false;
    if (!tomsEnabled && midiNote >= TOM_NOTE_MIN && midiNote <= TOM_NOTE_MAX)
        return false;
    if (!cymbalsEnabled && midiNote >= CYMBAL_NOTE_MIN && midiNote <= CYMBAL_NOTE_MAX)
        return false;

    return true;
}

//==============================================================================
void DrummerEngine::generateMidiFromPattern(const DrumPattern* pattern,
                                             double patternOffset,
                                             double blockStartBeat,
                                             double blockEndBeat,
                                             double bpm,
                                             juce::MidiBuffer& midiOut,
                                             int blockSamples)
{
    if (pattern == nullptr || !pattern->isValid())
        return;

    double beatsPerSecond = bpm / 60.0;
    double samplesPerBeat = sampleRate / beatsPerSecond;

    // The pattern's MIDI data is in seconds, convert to beats
    for (int i = 0; i < pattern->midiData.getNumEvents(); ++i)
    {
        auto* event = pattern->midiData.getEventPointer(i);
        auto& msg = event->message;

        if (!msg.isNoteOnOrOff())
            continue;

        // Convert event time from seconds to beats
        double eventTimeSeconds = event->message.getTimeStamp();
        double eventBeat = eventTimeSeconds * beatsPerSecond;

        // Adjust for pattern position
        double absoluteBeat = patternOffset + eventBeat;

        // Check if event falls within this block
        if (absoluteBeat >= blockStartBeat && absoluteBeat < blockEndBeat)
        {
            // Filter instruments
            if (!shouldPlayNote(msg.getNoteNumber()))
                continue;

            // Apply velocity scaling based on loudness
            int velocity = msg.getVelocity();
            velocity = static_cast<int>(velocity * (0.5f + loudness * 0.5f));
            velocity = juce::jlimit(1, 127, velocity);

            // Calculate sample position within block
            double beatOffset = absoluteBeat - blockStartBeat;
            int samplePos = static_cast<int>(beatOffset * samplesPerBeat);
            samplePos = juce::jlimit(0, blockSamples - 1, samplePos);

            // Create MIDI message
            if (msg.isNoteOn())
            {
                midiOut.addEvent(juce::MidiMessage::noteOn(10, msg.getNoteNumber(), (juce::uint8)velocity), samplePos);
            }
            else if (msg.isNoteOff())
            {
                midiOut.addEvent(juce::MidiMessage::noteOff(10, msg.getNoteNumber()), samplePos);
            }
        }
    }
}

//==============================================================================
void DrummerEngine::process(int numSamples, double bpm, double positionInBeats,
                             juce::MidiBuffer& midiOut)
{
    if (bpm <= 0)
        bpm = 120.0;

    double beatsPerSecond = bpm / 60.0;
    double samplesPerBeat = sampleRate / beatsPerSecond;
    double blockLengthBeats = numSamples / samplesPerBeat;

    double blockStartBeat = positionInBeats;
    double blockEndBeat = positionInBeats + blockLengthBeats;

    // Calculate current bar
    currentBar = static_cast<int>(positionInBeats / 4.0);

    // Select pattern if needed
    if (currentPattern == nullptr)
    {
        selectNewPattern();
        patternStartBeat = std::floor(positionInBeats / 4.0) * 4.0;  // Start at bar boundary
    }

    // Check for fill trigger
    if (fillRequested || shouldAutoFill(positionInBeats))
    {
        if (!inFill)
        {
            selectFillPattern();
            if (currentFillPattern != nullptr)
            {
                inFill = true;
                fillStartBeat = std::floor(positionInBeats);  // Start on beat
            }
        }
        fillRequested = false;
    }

    // Generate MIDI
    if (inFill && currentFillPattern != nullptr)
    {
        // Use metadata bars for fill length if lengthInBeats seems wrong
        double fillLength = currentFillPattern->lengthInBeats;
        if (fillLength < 2.0)  // Sanity check - fills should be at least half a bar
            fillLength = currentFillPattern->metadata.bars * 4.0;  // Use bars from metadata
        if (fillLength < 2.0)
            fillLength = 4.0;  // Default to 1 bar

        // Check if fill has ended
        if (positionInBeats >= fillStartBeat + fillLength)
        {
            inFill = false;
            lastFillEndBeat = positionInBeats;  // Record when fill ended for cooldown
            currentFillPattern = nullptr;
            // Reset pattern to start fresh after fill
            patternStartBeat = std::floor(positionInBeats / 4.0) * 4.0;
        }
        else
        {
            generateMidiFromPattern(currentFillPattern, fillStartBeat,
                                     blockStartBeat, blockEndBeat, bpm,
                                     midiOut, numSamples);
        }
    }
    else if (currentPattern != nullptr)
    {
        // Loop the pattern - use metadata bars if lengthInBeats seems wrong
        double patternLength = currentPattern->lengthInBeats;
        if (patternLength < 4.0)  // Sanity check - patterns should be at least 1 bar
            patternLength = currentPattern->metadata.bars * 4.0;
        if (patternLength < 4.0)
            patternLength = 16.0;  // Default to 4 bars

        // Calculate position within looped pattern
        double patternPosition = std::fmod(positionInBeats - patternStartBeat, patternLength);
        if (patternPosition < 0)
            patternPosition += patternLength;

        double adjustedStart = patternStartBeat + std::floor((positionInBeats - patternStartBeat) / patternLength) * patternLength;

        generateMidiFromPattern(currentPattern, adjustedStart,
                                 blockStartBeat, blockEndBeat, bpm,
                                 midiOut, numSamples);
    }

    // Check if we should select a new pattern (at phrase boundaries)
    if (currentBar != lastProcessedBar && currentBar % 8 == 0)
    {
        // Consider pattern change at 8-bar boundaries
        // For now, keep the same pattern - ML model will handle this later
    }

    lastProcessedBar = currentBar;
}

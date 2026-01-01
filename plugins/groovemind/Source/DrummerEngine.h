/*
  ==============================================================================

    DrummerEngine.h
    Core engine that generates MIDI drum patterns based on parameters

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PatternLibrary.h"

//==============================================================================
// Style presets
enum class DrummerStyle
{
    Rock = 0,
    Pop,
    Funk,
    Soul,
    Jazz,
    Blues,
    HipHop,
    RnB,
    Electronic,
    Latin,
    Country,
    Punk,
    NumStyles
};

// Drummer personalities
enum class DrummerPersonality
{
    Alex = 0,      // Versatile, adapts well
    Jordan,        // Groovy, pocket-focused
    Sam,           // Steady, reliable
    Riley,         // Energetic, lots of fills
    Casey,         // Technical, complex patterns
    Morgan,        // Jazz-influenced, brushes
    NumDrummers
};

// Kit types
enum class KitType
{
    Acoustic = 0,
    Brush,
    Electronic,
    Hybrid,
    NumKits
};

// Song sections
enum class SongSection
{
    Intro = 0,
    Verse,
    PreChorus,
    Chorus,
    Bridge,
    Breakdown,
    Outro,
    NumSections
};

//==============================================================================
class DrummerEngine
{
public:
    DrummerEngine(PatternLibrary& library);
    ~DrummerEngine() = default;

    // Prepare for playback
    void prepare(double sampleRate, int samplesPerBlock);

    // Main processing - generates MIDI events
    void process(int numSamples, double bpm, double positionInBeats, juce::MidiBuffer& midiOut);

    // Parameter setters
    void setStyle(int styleIndex);
    void setDrummer(int drummerIndex);
    void setSection(int sectionIndex);
    void setKit(int kitIndex);
    void setComplexity(float value);
    void setLoudness(float value);
    void setEnergy(float value);

    // Fill control
    void triggerFill(int lengthInBeats = 4);
    void setFillMode(int mode);  // 0=auto, 1=manual, 2=off
    void setFillIntensity(float value);

    // Instrument toggles
    void setKickEnabled(bool enabled) { kickEnabled = enabled; }
    void setSnareEnabled(bool enabled) { snareEnabled = enabled; }
    void setHihatEnabled(bool enabled) { hihatEnabled = enabled; }
    void setTomsEnabled(bool enabled) { tomsEnabled = enabled; }
    void setCymbalsEnabled(bool enabled) { cymbalsEnabled = enabled; }

    // State queries
    bool isPlayingFill() const { return inFill; }
    int getCurrentBar() const { return currentBar; }
    const DrumPattern* getCurrentPattern() const { return currentPattern; }

private:
    PatternLibrary& patternLibrary;

    // Current state
    DrummerStyle currentStyle = DrummerStyle::Rock;
    DrummerPersonality currentDrummer = DrummerPersonality::Alex;
    SongSection currentSection = SongSection::Verse;
    KitType currentKit = KitType::Acoustic;

    float complexity = 0.5f;
    float loudness = 0.5f;
    float energy = 0.6f;

    // Fill state
    bool inFill = false;
    int fillMode = 0;  // 0=auto, 1=manual, 2=off
    float fillIntensity = 0.5f;
    int fillLengthBeats = 4;
    double fillStartBeat = 0.0;
    bool fillRequested = false;
    double lastFillEndBeat = -100.0;  // Cooldown tracking

    // Instrument enables
    bool kickEnabled = true;
    bool snareEnabled = true;
    bool hihatEnabled = true;
    bool tomsEnabled = true;
    bool cymbalsEnabled = true;

    // Current pattern tracking
    const DrumPattern* currentPattern = nullptr;
    const DrumPattern* currentFillPattern = nullptr;
    double patternStartBeat = 0.0;
    int currentBar = 0;
    int lastProcessedBar = -1;

    // Sample rate
    double sampleRate = 44100.0;

    // Build pattern query from current parameters
    PatternQuery buildQuery() const;

    // Check if we should trigger an auto-fill
    bool shouldAutoFill(double positionInBeats) const;

    // Select appropriate pattern
    void selectNewPattern();
    void selectFillPattern();

    // Generate MIDI events from pattern
    void generateMidiFromPattern(const DrumPattern* pattern,
                                  double patternOffset,
                                  double blockStartBeat,
                                  double blockEndBeat,
                                  double bpm,
                                  juce::MidiBuffer& midiOut,
                                  int blockSamples);

    // Filter MIDI based on instrument enables
    bool shouldPlayNote(int midiNote) const;

    // GM Drum note ranges
    static constexpr int KICK_NOTE_MIN = 35;
    static constexpr int KICK_NOTE_MAX = 36;
    static constexpr int SNARE_NOTE_MIN = 37;
    static constexpr int SNARE_NOTE_MAX = 40;
    static constexpr int HIHAT_NOTE_MIN = 42;
    static constexpr int HIHAT_NOTE_MAX = 46;
    static constexpr int TOM_NOTE_MIN = 41;
    static constexpr int TOM_NOTE_MAX = 50;
    static constexpr int CYMBAL_NOTE_MIN = 49;
    static constexpr int CYMBAL_NOTE_MAX = 57;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrummerEngine)
};

/*
  ==============================================================================

    PatternLibrary.h
    Manages the collection of drum patterns with metadata-based selection

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
// Pattern metadata matching the JSON schema
struct PatternMetadata
{
    juce::String id;
    juce::String name;
    juce::String style;
    juce::String substyle;
    int tempoBpm = 120;
    int tempoRangeMin = 80;
    int tempoRangeMax = 160;
    juce::String tempoFeel;
    juce::String timeSignature = "4/4";
    juce::String type;  // beat, fill, intro, outro, etc.
    juce::String section;
    int bars = 4;
    float energy = 0.5f;
    float complexity = 0.5f;

    // Groove characteristics
    float swing = 0.0f;
    float pushPull = 0.0f;
    float tightness = 0.5f;

    juce::String kit;  // acoustic, brush, electronic, hybrid

    // Instrument flags
    bool hasKick = true;
    bool hasSnare = true;
    bool hasHihat = true;
    bool hasRide = false;
    bool hasCrash = false;
    bool hasToms = false;

    // Articulation flags
    bool hasGhostNotes = false;
    bool hasBrushSweeps = false;
    bool hasCrossStick = false;

    // Source info
    juce::String dataset;
    juce::String sourceFile;
    juce::String drummerId;

    // Tags for search
    juce::StringArray tags;

    // ML features
    float velocityMean = 64.0f;
    float velocityStd = 20.0f;
    float noteDensity = 8.0f;
};

//==============================================================================
// A single MIDI pattern with its data
struct DrumPattern
{
    PatternMetadata metadata;
    juce::MidiMessageSequence midiData;
    double lengthInBeats = 16.0;

    bool isValid() const { return midiData.getNumEvents() > 0; }
};

//==============================================================================
// Pattern selection criteria
struct PatternQuery
{
    juce::String style = "";
    juce::String kit = "acoustic";
    juce::String type = "beat";
    juce::String section = "";
    float targetEnergy = 0.5f;
    float targetComplexity = 0.5f;
    int targetTempo = 120;
    bool requireBrushSweeps = false;

    // Weighting for matching
    float energyWeight = 1.0f;
    float complexityWeight = 1.0f;
    float tempoWeight = 0.5f;
};

//==============================================================================
class PatternLibrary
{
public:
    PatternLibrary();
    ~PatternLibrary() = default;

    // Load patterns from directory
    bool loadFromDirectory(const juce::File& directory);

    // Load patterns from embedded binary data
    bool loadFromBinaryData(const void* data, size_t size);

    // Pattern selection
    const DrumPattern* selectPattern(const PatternQuery& query) const;
    const DrumPattern* selectFill(const PatternQuery& query, int fillLengthBeats = 4) const;
    const DrumPattern* getRandomPattern(const juce::String& style, const juce::String& type) const;

    // Pattern retrieval
    const DrumPattern* getPatternById(const juce::String& id) const;
    int getPatternCount() const { return patterns.size(); }

    // Style information
    juce::StringArray getAvailableStyles() const;
    juce::StringArray getAvailableKits() const;

    // Query helpers
    std::vector<const DrumPattern*> findMatchingPatterns(const PatternQuery& query, int maxResults = 10) const;

private:
    std::vector<DrumPattern> patterns;
    std::map<juce::String, int> patternIdIndex;  // id -> index

    // Loading helpers
    bool loadPatternMetadata(const juce::File& jsonFile);
    bool loadMidiPattern(const juce::File& midiFile, DrumPattern& pattern);
    PatternMetadata parseMetadataJson(const juce::var& json);

    // Scoring for pattern selection
    float scorePattern(const DrumPattern& pattern, const PatternQuery& query) const;

    // Random selection with history to avoid repetition
    mutable std::vector<juce::String> recentPatternIds;
    static constexpr int maxRecentHistory = 8;

    mutable juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternLibrary)
};

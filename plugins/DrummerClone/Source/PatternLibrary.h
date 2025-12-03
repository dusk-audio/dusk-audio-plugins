#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>
#include <map>
#include <random>
#include "DrumMapping.h"

/**
 * DrumHit - A single drum hit in a pattern
 */
struct DrumHit
{
    int tick = 0;                // Position in ticks (960 PPQ)
    DrumMapping::DrumElement element = DrumMapping::KICK;  // What drum
    int velocity = 100;          // MIDI velocity 1-127
    int duration = 120;          // Duration in ticks

    DrumHit() = default;

    DrumHit(int t, DrumMapping::DrumElement e, int v, int d)
        : tick(juce::jmax(0, t))
        , element(e)
        , velocity(juce::jlimit(1, 127, v))
        , duration(juce::jmax(0, d))
    {}

    // For sorting
    bool operator<(const DrumHit& other) const { return tick < other.tick; }
};
/**
 * PatternPhrase - A musical drum phrase (typically 1-4 bars)
 *
 * This is the core unit of the pattern library. Instead of algorithmically
 * generating patterns, we select and vary pre-composed phrases.
 */
struct PatternPhrase
{
    juce::String id;             // Unique identifier
    juce::String style;          // Rock, HipHop, etc.
    juce::String category;       // groove, fill, intro, ending
    juce::String tags;           // Comma-separated: "heavy,syncopated,ghost-notes"

    int bars = 1;                // Length in bars
    int timeSigNum = 4;          // Time signature numerator
    int timeSigDenom = 4;        // Time signature denominator

    // Characteristics (0.0 - 1.0)
    float energy = 0.5f;         // Overall energy level
    float density = 0.5f;        // Note density
    float syncopation = 0.0f;    // Amount of offbeat emphasis
    float ghostNoteDensity = 0.0f;  // Ghost note presence
    float swing = 0.0f;          // Swing amount (0 = straight)

    // The actual hits
    std::vector<DrumHit> hits;

    // Metadata
    juce::String source;         // Where this pattern came from
    juce::String author;         // Attribution if needed

    // Validity check
    bool isValid() const { return !hits.empty() && bars > 0 && timeSigNum > 0 && timeSigDenom > 0; }
    // Get hits for a specific drum element
    std::vector<DrumHit> getHitsForElement(DrumMapping::DrumElement elem) const
    {
        std::vector<DrumHit> result;
        for (const auto& hit : hits)
        {
            if (hit.element == elem)
                result.push_back(hit);
        }
        return result;
    }

    // Check if phrase has specific element
    bool hasElement(DrumMapping::DrumElement elem) const
    {
        for (const auto& hit : hits)
        {
            if (hit.element == elem)
                return true;
        }
        return false;
    }

    // Calculate characteristics from hits
    void calculateCharacteristics(int ppq = 960);
};

/**
 * Fill context for intelligent fill selection (Phase 4)
 */
enum class FillContext
{
    Standard,       // Normal fill
    BuildUp,        // Building tension before chorus
    TensionRelease, // Releasing after buildup
    SectionStart,   // Start of new section
    SectionEnd,     // End of section (going to new section)
    Breakdown,      // Minimal, sparse fill
    Outro           // Ending fill
};

/**
 * PatternLibrary - Manages a collection of drum patterns
 *
 * Supports loading from:
 * - JSON pattern files (our format)
 * - Standard MIDI files (Type 0 or Type 1)
 *
 * Provides pattern selection based on style, energy, density, etc.
 *
 * Phase 4 Enhancements:
 * - Context-aware fill selection (tension/release)
 * - Leading tone generation for smooth transitions
 * - Section-aware intensity scaling
 */
class PatternLibrary
{
public:
    PatternLibrary();
    ~PatternLibrary() = default;

    /**
     * Load patterns from a directory
     * Loads both .json and .mid files
     * @param directory Path to pattern directory
     * @return Number of patterns loaded
     */
    int loadFromDirectory(const juce::File& directory);

    /**
     * Load patterns from embedded binary data
     * @param data Binary data (JSON array of patterns)
     * @param size Size of data
     * @return Number of patterns loaded
     */
    int loadFromBinaryData(const void* data, int size);

    /**
     * Load a single JSON pattern file
     * @param file JSON file
     * @return true if successful
     */
    bool loadPatternJSON(const juce::File& file);

    /**
     * Load patterns from a MIDI file
     * Attempts to extract drum patterns from MIDI
     * @param file MIDI file
     * @param style Style to assign to patterns
     * @return Number of patterns extracted
     */
    int loadFromMIDI(const juce::File& file, const juce::String& style = "Unknown");

    /**
     * Save a pattern to JSON
     * @param pattern Pattern to save
     * @param file Output file
     * @return true if successful
     */
    static bool savePatternJSON(const PatternPhrase& pattern, const juce::File& file);

    /**
     * Find patterns matching criteria
     * @param style Style filter (empty = any)
     * @param category Category filter (empty = any)
     * @param minEnergy Minimum energy (0-1)
     * @param maxEnergy Maximum energy (0-1)
     * @param minDensity Minimum density (0-1)
     * @param maxDensity Maximum density (0-1)
     * @return Vector of matching pattern indices
     */
    std::vector<int> findPatterns(const juce::String& style = "",
                                   const juce::String& category = "",
                                   float minEnergy = 0.0f,
                                   float maxEnergy = 1.0f,
                                   float minDensity = 0.0f,
                                   float maxDensity = 1.0f) const;

    /**
     * Select best matching pattern for context
     * @param style Preferred style
     * @param targetEnergy Target energy level
     * @param targetDensity Target density
     * @param avoidRecent Avoid recently used patterns
     * @return Pattern index (-1 if none found)
     */
    int selectBestPattern(const juce::String& style,
                          float targetEnergy,
                          float targetDensity,
                          bool avoidRecent = true);

    /**
     * Select a fill pattern
     * @param style Preferred style
     * @param beats Fill length in beats (1, 2, or 4)
     * @param intensity Fill intensity (0-1)
     * @return Pattern index (-1 if none found)
     */
    int selectFillPattern(const juce::String& style,
                          int beats,
                          float intensity);

    /**
     * Phase 4: Select a context-aware fill pattern
     * Takes into account musical context for more appropriate fills
     * @param style Preferred style
     * @param beats Fill length in beats
     * @param intensity Base intensity (0-1)
     * @param context Musical context (buildup, release, etc.)
     * @param nextSectionEnergy Energy level of upcoming section (0-1)
     * @return Pattern index (-1 if none found)
     */
    int selectContextualFill(const juce::String& style,
                             int beats,
                             float intensity,
                             FillContext context,
                             float nextSectionEnergy = 0.5f);

    /**
     * Phase 4: Generate leading tone hits for fill
     * Creates anticipatory notes before the fill starts
     * @param fillPattern The fill pattern being used
     * @param numBeats Number of beats before fill to add leading tones
     * @param bpm Current tempo
     * @return Vector of leading tone hits
     */
    std::vector<DrumHit> generateLeadingTones(const PatternPhrase& fillPattern,
                                               int numBeats,
                                               double bpm);

    /**
     * Phase 4: Generate transition hits for smooth section changes
     * @param fromEnergy Current section energy
     * @param toEnergy Next section energy
     * @param beats Number of beats for transition
     * @return Pattern of transition hits
     */
    PatternPhrase generateTransition(float fromEnergy,
                                      float toEnergy,
                                      int beats);

    /**
     * Get pattern by index
     */
    const PatternPhrase& getPattern(int index) const;

    /**
     * Get total number of patterns
     */
    int getNumPatterns() const { return static_cast<int>(patterns.size()); }

    /**
     * Mark pattern as recently used (for avoiding repetition)
     */
    void markUsed(int index);

    /**
     * Clear recently used history
     */
    void clearHistory();

    /**
     * Check if library has patterns for a style
     */
    bool hasStyle(const juce::String& style) const;

    /**
     * Get list of available styles
     */
    juce::StringArray getAvailableStyles() const;

    /**
     * Add built-in patterns (called automatically if no patterns loaded)
     */
    void loadBuiltInPatterns();

    /**
     * Get patterns grouped by style
     */
    std::map<juce::String, std::vector<int>> getPatternsByStyle() const;

private:
    std::vector<PatternPhrase> patterns;
    PatternPhrase emptyPattern;  // Returned when no pattern found

    // Recently used pattern tracking
    static constexpr int HISTORY_SIZE = 16;
    std::array<int, HISTORY_SIZE> recentlyUsed;
    int historyIndex = 0;

    // Random for selection
    std::mt19937 rng;

    // Helper to parse JSON pattern
    PatternPhrase parsePatternJSON(const juce::var& json);

    // Helper to extract patterns from MIDI
    std::vector<PatternPhrase> extractPatternsFromMIDI(const juce::MidiFile& midiFile,
                                                        const juce::String& style);

    // Calculate match score between pattern and target
    float calculateMatchScore(const PatternPhrase& pattern,
                              const juce::String& style,
                              float targetEnergy,
                              float targetDensity) const;

    // Check if pattern was recently used
    bool wasRecentlyUsed(int index) const;

    // Create built-in patterns for each style
    void createRockPatterns();
    void createHipHopPatterns();
    void createAlternativePatterns();
    void createRnBPatterns();
    void createElectronicPatterns();
    void createTrapPatterns();
    void createSongwriterPatterns();
    void createFillPatterns();
};

/**
 * PatternVariator - Applies variations to patterns
 *
 * Takes a base pattern and creates musical variations without
 * completely changing its character.
 */
class PatternVariator
{
public:
    PatternVariator();

    /**
     * Apply velocity variation (humanization)
     * @param pattern Pattern to modify (in place)
     * @param amount Variation amount (0-1)
     * @param useGaussian Use gaussian distribution (more natural)
     */
    void applyVelocityVariation(PatternPhrase& pattern, float amount, bool useGaussian = true);

    /**
     * Apply timing variation (humanization)
     * @param pattern Pattern to modify (in place)
     * @param amountMs Max timing shift in milliseconds
     * @param bpm Current tempo (for tick conversion)
     * @param useGaussian Use gaussian distribution
     */
    void applyTimingVariation(PatternPhrase& pattern, float amountMs, double bpm, bool useGaussian = true);

    /**
     * Apply per-instrument timing characteristics
     * Different instruments have different timing feels
     * @param pattern Pattern to modify
     * @param bpm Current tempo
     */
    void applyInstrumentTiming(PatternPhrase& pattern, double bpm);

    /**
     * Substitute some drum hits with alternatives
     * e.g., occasional open hi-hat instead of closed
     * @param pattern Pattern to modify
     * @param probability Substitution probability (0-1)
     */
    void applySubstitutions(PatternPhrase& pattern, float probability);

    /**
     * Add or remove ghost notes
     * @param pattern Pattern to modify
     * @param targetDensity Target ghost note density (0-1)
     */
    void adjustGhostNotes(PatternPhrase& pattern, float targetDensity);

    /**
     * Apply swing to pattern
     * @param pattern Pattern to modify
     * @param swing Swing amount (0 = straight, 0.5 = triplet)
     * @param division Division to swing (8 or 16)
     */
    void applySwing(PatternPhrase& pattern, float swing, int division = 16);

    /**
     * Scale pattern energy (velocity)
     * @param pattern Pattern to modify
     * @param scale Energy scale factor
     */
    void scaleEnergy(PatternPhrase& pattern, float scale);

    /**
     * Combined humanization - the main entry point
     * @param pattern Pattern to modify
     * @param timingVar Timing variation amount (0-100)
     * @param velocityVar Velocity variation amount (0-100)
     * @param bpm Current tempo
     */
    void humanize(PatternPhrase& pattern, float timingVar, float velocityVar, double bpm);

private:
    std::mt19937 rng;
    std::normal_distribution<float> gaussianDist{0.0f, 1.0f};
    std::uniform_real_distribution<float> uniformDist{0.0f, 1.0f};

    // Per-instrument timing characteristics (in ms)
    struct InstrumentTiming
    {
        float meanOffset;     // Average offset from grid
        float stdDev;         // Standard deviation
        float velocityScale;  // Velocity multiplier
    };

    std::map<DrumMapping::DrumElement, InstrumentTiming> instrumentTimings;

    void initInstrumentTimings();
};

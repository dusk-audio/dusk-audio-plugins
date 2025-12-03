#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "DrumMapping.h"
#include "GrooveTemplateGenerator.h"
#include "DrummerDNA.h"
#include "VariationEngine.h"
#include "PatternLibrary.h"

/**
 * Section types for pattern variation
 */
enum class DrumSection
{
    Intro = 0,
    Verse,
    PreChorus,
    Chorus,
    Bridge,
    Breakdown,
    Outro
};

/**
 * Humanization settings from UI
 */
struct HumanizeSettings
{
    float timingVariation = 20.0f;   // 0-100%
    float velocityVariation = 15.0f; // 0-100%
    float pushDrag = 0.0f;           // -50 to +50
    float grooveDepth = 50.0f;       // 0-100%
};

/**
 * Fill settings from UI
 */
struct FillSettings
{
    float frequency = 30.0f;    // 0-100% chance per bar
    float intensity = 50.0f;    // 0-100%
    int lengthBeats = 1;        // 1, 2, or 4 beats
    bool manualTrigger = false; // Manual trigger button pressed
};

/**
 * DrummerEngine - Core MIDI drum pattern generator
 *
 * Generates intelligent, musical drum patterns based on:
 * - Style selection (Rock, HipHop, etc.)
 * - Complexity/loudness parameters
 * - Follow Mode groove templates
 * - Procedural variation for natural feel
 */
class DrummerEngine
{
public:
    DrummerEngine(juce::AudioProcessorValueTreeState& params);
    ~DrummerEngine() = default;

    /**
     * Prepare the engine for playback
     * @param sampleRate Audio sample rate
     * @param samplesPerBlock Maximum block size
     */
    void prepare(double sampleRate, int samplesPerBlock);

    // Type alias for backward compatibility
    using Section = DrumSection;

    /**
     * Generate a region of drum MIDI
     * @param bars Number of bars to generate
     * @param bpm Tempo in beats per minute
     * @param styleIndex Style index (0-6)
     * @param groove Groove template from Follow Mode
     * @param complexity Complexity parameter (1-10)
     * @param loudness Loudness parameter (0-100)
     * @param swingOverride Swing override parameter (0-100)
     * @param section Current section type
     * @param humanize Humanization settings
     * @param fill Fill settings
     * @return MidiBuffer containing generated drum events
     */
    juce::MidiBuffer generateRegion(int bars,
                                    double bpm,
                                    int styleIndex,
                                    const GrooveTemplate& groove,
                                    float complexity,
                                    float loudness,
                                    float swingOverride,
                                    DrumSection section = DrumSection::Verse,
                                    HumanizeSettings humanize = HumanizeSettings(),
                                    FillSettings fill = FillSettings());

    /**
     * Generate a fill
     * @param beats Length of fill in beats
     * @param bpm Tempo
     * @param intensity Fill intensity (0.0 - 1.0)
     * @param startTick Starting tick position
     * @return MidiBuffer containing fill
     */
    juce::MidiBuffer generateFill(int beats, double bpm, float intensity, int startTick);

    /**
     * Step sequencer lane indices - eight lanes (Kick through Crash).
     * SEQ_NUM_LANES is a sentinel value representing the lane count.
     *
     * IMPORTANT: These values must remain synchronized with StepSequencer::DrumLane
     * in StepSequencer.h. Any changes to ordering or values must be reflected in both.
     */
    enum class StepSeqLane : int
    {
        SEQ_KICK = 0,
        SEQ_SNARE = 1,
        SEQ_CLOSED_HIHAT = 2,
        SEQ_OPEN_HIHAT = 3,
        SEQ_CLAP = 4,
        SEQ_TOM1 = 5,
        SEQ_TOM2 = 6,
        SEQ_CRASH = 7,
        SEQ_NUM_LANES = 8
    };

    /** Number of steps in the step sequencer (16th notes per bar) */
    static constexpr int STEP_SEQUENCER_STEPS = 16;

    /** Number of lanes in the step sequencer (8 lanes: Kick through Crash) */
    static constexpr int STEP_SEQUENCER_LANES = static_cast<int>(StepSeqLane::SEQ_NUM_LANES);

    /**
     * Generate MIDI from step sequencer pattern
     *
     * @param pattern Step pattern data indexed as pattern[lane][step], where:
     *                - lane: 0 to STEP_SEQUENCER_LANES-1 (use StepSeqLane enum)
     *                - step: 0 to STEP_SEQUENCER_STEPS-1 (16th note positions)
     *                Each element is std::pair<bool, float>:
     *                - first (bool): true if step is active/enabled
     *                - second (float): velocity from 0.0 to 1.0 (maps to MIDI 1-127)
     * @param bpm Tempo in beats per minute (must be > 0)
     * @param humanize Humanization settings (timing/velocity variation)
     * @return MidiBuffer containing the generated MIDI pattern
     */
    juce::MidiBuffer generateFromStepSequencer(
        const std::array<std::array<std::pair<bool, float>, STEP_SEQUENCER_STEPS>, STEP_SEQUENCER_LANES>& pattern,
        double bpm,
        HumanizeSettings humanize = HumanizeSettings());

    /**
     * Set the drummer "personality" index
     * @param index Drummer index (affects style bias)
     */
    void setDrummer(int index);

    /**
     * Reset the engine state
     */
    void reset();

    /**
     * Kit piece enable/disable (for filtering output)
     */
    struct KitEnableMask
    {
        bool kick = true;
        bool snare = true;
        bool hihat = true;
        bool toms = true;
        bool cymbals = true;
        bool percussion = true;
    };

    /**
     * Set which kit pieces are enabled (affects output generation)
     */
    void setKitEnableMask(const KitEnableMask& mask) { kitMask = mask; }

    /**
     * Get current kit enable mask
     */
    const KitEnableMask& getKitEnableMask() const { return kitMask; }

    /**
     * Check if a drum element is enabled based on kit mask
     */
    bool isElementEnabled(DrumMapping::DrumElement element) const;

    /**
     * Get the MIDI note map (for reading current mappings)
     */
    const DrumMapping::MidiNoteMap& getMidiNoteMap() const { return midiNoteMap; }

    /**
     * Get a mutable reference to the MIDI note map (for modifying mappings)
     */
    DrumMapping::MidiNoteMap& getMidiNoteMap() { return midiNoteMap; }

    /**
     * Set a custom MIDI note for a drum element
     * @param element The drum element to map
     * @param midiNote MIDI note number (0-127)
     */
    void setMidiNote(DrumMapping::DrumElement element, int midiNote) {
        if (midiNote < 0 || midiNote > 127) {
            DBG("DrummerEngine::setMidiNote: Invalid MIDI note " << midiNote << ", clamping to 0-127");
            midiNote = juce::jlimit(0, 127, midiNote);
        }
        midiNoteMap.setNoteForElement(element, midiNote);
    }

    /**
     * Get the MIDI note for a drum element (uses custom mapping)
     * @param element The drum element
     * @return MIDI note number
     */
    int getMidiNote(DrumMapping::DrumElement element) const {
        return midiNoteMap.getNoteForElement(element);
    }

    /**
     * Load a preset MIDI mapping
     * @param preset Preset name: "GM", "SuperiorDrummer", "EZdrummer", "SSD", "BFD"
     */
    void loadMidiPreset(const juce::String& preset) {
        if (preset == "SuperiorDrummer")
            midiNoteMap.loadSuperiorDrummerMapping();
        else if (preset == "EZdrummer")
            midiNoteMap.loadEZdrummerMapping();
        else if (preset == "SSD")
            midiNoteMap.loadSSDMapping();
        else if (preset == "BFD")
            midiNoteMap.loadBFDMapping();
        else {
            if (!preset.isEmpty() && preset != "GM") {
                DBG("DrummerEngine::loadMidiPreset: Unknown preset '" << preset << "', defaulting to GM");
            }
            midiNoteMap.resetToDefaults();  // GM default
        }
    }

    /**
     * Set the time signature (from DAW transport)
     *
     * Validates inputs to prevent division by zero and invalid time signatures:
     * - numerator must be > 0 (clamped to 1 if invalid)
     * - denominator must be a power of two (1,2,4,8,16,...); clamped to nearest valid value
     *
     * @param numerator Beats per bar (must be > 0)
     * @param denominator Beat unit (4 = quarter note, 8 = eighth note); must be power of 2
     */
    void setTimeSignature(int numerator, int denominator) {
        // Validate numerator: must be positive
        if (numerator <= 0) {
            DBG("DrummerEngine::setTimeSignature: Invalid numerator " << numerator << ", clamping to 1");
            numerator = 1;
        }

        // Validate denominator: must be positive power of two
        if (denominator <= 0) {
            DBG("DrummerEngine::setTimeSignature: Invalid denominator " << denominator << ", defaulting to 4");
            denominator = 4;
        } else if ((denominator & (denominator - 1)) != 0) {
            // Not a power of two - find nearest power of two
            int original = denominator;
            int lower = 1;
            while (lower * 2 < denominator) lower *= 2;
            int upper = lower * 2;
            denominator = (denominator - lower < upper - denominator) ? lower : upper;
            DBG("DrummerEngine::setTimeSignature: Denominator " << original
                << " is not a power of two, clamping to " << denominator);
        }

        timeSignatureNumerator = numerator;
        timeSignatureDenominator = denominator;
    }

    // PPQ resolution (ticks per quarter note)
    static constexpr int PPQ = 960;

private:
    juce::AudioProcessorValueTreeState& parameters;

    // Engine state
    double sampleRate = 44100.0;
    int samplesPerBlock = 512;
    int currentDrummer = 0;
    juce::Random random;

    // Drummer personality system
    DrummerDNA drummerDNA;
    DrummerProfile currentProfile;
    VariationEngine variationEngine;
    int barsSinceLastFill = 0;

    // Pattern library system (Phase 2) - lazily initialized
    std::unique_ptr<PatternLibrary> patternLibrary;
    std::unique_ptr<PatternVariator> patternVariator;
    bool usePatternLibrary = false;  // Driven by parameter, default off until initialized
    bool patternLibraryInitialized = false;  // Track initialization state
    bool patternLibraryFailed = false;  // Track if initialization failed

    // Lazy initialization of pattern library
    void initPatternLibraryIfNeeded();

    // Cached parameter pointer for usePatternLibrary setting
    std::atomic<float>* usePatternLibraryParam = nullptr;

    // Pattern-based generation methods
    juce::MidiBuffer generateFromPatternLibrary(int bars, double bpm,
                                                 const juce::String& style,
                                                 const GrooveTemplate& groove,
                                                 float complexity, float loudness,
                                                 DrumSection section,
                                                 HumanizeSettings humanize);

    juce::MidiBuffer generateFillFromLibrary(int beats, double bpm,
                                              float intensity,
                                              const juce::String& style,
                                              int startTick);

    // Convert PatternPhrase to MidiBuffer with humanization
    juce::MidiBuffer patternToMidi(const PatternPhrase& pattern,
                                    double bpm,
                                    const GrooveTemplate& groove,
                                    const HumanizeSettings& humanize,
                                    int tickOffset = 0);

    // Style names for lookup
    const juce::StringArray styleNames = {
        "Rock", "HipHop", "Alternative", "R&B", "Electronic", "Trap", "Songwriter"
    };

    // Generation methods
    void generateKickPattern(juce::MidiBuffer& buffer, int bars, double bpm,
                            const DrumMapping::StyleHints& hints, const GrooveTemplate& groove,
                            float complexity, float loudness);

    void generateSnarePattern(juce::MidiBuffer& buffer, int bars, double bpm,
                             const DrumMapping::StyleHints& hints, const GrooveTemplate& groove,
                             float complexity, float loudness);

    void generateHiHatPattern(juce::MidiBuffer& buffer, int bars, double bpm,
                             const DrumMapping::StyleHints& hints, const GrooveTemplate& groove,
                             float complexity, float loudness);

    void generateTrapHiHats(juce::MidiBuffer& buffer, int bars, double bpm,
                           float loudnessScale, float complexity);

    void generateCymbals(juce::MidiBuffer& buffer, int bars, double bpm,
                        const DrumMapping::StyleHints& hints, const GrooveTemplate& groove,
                        float complexity, float loudness);

    void generateGhostNotes(juce::MidiBuffer& buffer, int bars, double bpm,
                           const DrumMapping::StyleHints& hints, const GrooveTemplate& groove,
                           float complexity);

    void generatePercussionPattern(juce::MidiBuffer& buffer, int bars, double bpm,
                                   const DrumMapping::StyleHints& hints, const GrooveTemplate& groove,
                                   float complexity, float loudness);

    // Time signature (set from processor, defaults to 4/4)
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;

    // Timing helpers - use current time signature
    int ticksPerBar(double bpm) const {
        (void)bpm; // Unused but kept for API compatibility
        // PPQ is ticks per quarter note
        // For 4/4: 4 beats * PPQ = 4 * PPQ
        // For 3/4: 3 beats * PPQ = 3 * PPQ
        // For 6/8: 6 eighth notes = 3 quarter notes = 3 * PPQ
        return PPQ * timeSignatureNumerator * 4 / timeSignatureDenominator;
    }
    int beatsPerBar() const { return timeSignatureNumerator; }
    int sixteenthsPerBar() const { return (timeSignatureNumerator * 16) / timeSignatureDenominator; }  // 16 for 4/4, 12 for 3/4
    int ticksPerBeat() const { return PPQ * 4 / timeSignatureDenominator; }
    int ticksPerEighth() const { return PPQ / 2; }
    int ticksPerSixteenth() const { return PPQ / 4; }

    // Apply groove to timing
    int applySwing(int tick, float swing, int division);
    int applyMicroTiming(int tick, const GrooveTemplate& groove, double bpm);
    int applyHumanization(int tick, int maxJitterTicks);

    // Velocity helpers
    int calculateVelocity(int basevelocity, float loudness, const GrooveTemplate& groove,
                         int tickPosition, int jitterRange = 10);

    // Probability helpers
    bool shouldTrigger(float probability);
    float getComplexityProbability(float complexity, float baseProb);

    // Section-based modifiers
    float getSectionDensityMultiplier(DrumSection section) const;
    float getSectionLoudnessMultiplier(DrumSection section) const;
    bool shouldAddCrashForSection(DrumSection section);

    // Humanization helpers
    int applyAdvancedHumanization(int tick, const HumanizeSettings& humanize, double bpm);
    int applyVelocityHumanization(int baseVel, const HumanizeSettings& humanize);

    // Current humanization settings (cached for use in generation methods)
    HumanizeSettings currentHumanize;

    // Configurable MIDI note mapping
    DrumMapping::MidiNoteMap midiNoteMap;

    // Kit piece enable/disable mask
    KitEnableMask kitMask;

    // MIDI helpers - uses midiNoteMap for note lookup
    void addNote(juce::MidiBuffer& buffer, int pitch, int velocity, int startTick, int durationTicks);

    // Helper to add note with kit mask filtering (skips if element is disabled)
    void addNoteFiltered(juce::MidiBuffer& buffer, DrumMapping::DrumElement element, int velocity, int startTick, int durationTicks);

    // Helper to get MIDI note for element using the configurable map
    int getNoteForElement(DrumMapping::DrumElement element) const {
        return midiNoteMap.getNoteForElement(element);
    }
};
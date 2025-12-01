#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "DrumMapping.h"
#include "GrooveTemplateGenerator.h"
#include "DrummerDNA.h"
#include "VariationEngine.h"

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

    void generateCymbals(juce::MidiBuffer& buffer, int bars, double bpm,
                        const DrumMapping::StyleHints& hints, const GrooveTemplate& groove,
                        float complexity, float loudness);

    void generateGhostNotes(juce::MidiBuffer& buffer, int bars, double bpm,
                           const DrumMapping::StyleHints& hints, const GrooveTemplate& groove,
                           float complexity);

    // Timing helpers
    int ticksPerBar(double bpm) const { return PPQ * 4; }  // 4/4 time
    int ticksPerBeat() const { return PPQ; }
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

    // MIDI helpers
    void addNote(juce::MidiBuffer& buffer, int pitch, int velocity, int startTick, int durationTicks);
};
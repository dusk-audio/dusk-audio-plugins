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

    /**
     * Generate a region of drum MIDI
     * @param bars Number of bars to generate
     * @param bpm Tempo in beats per minute
     * @param styleIndex Style index (0-6)
     * @param groove Groove template from Follow Mode
     * @param complexity Complexity parameter (1-10)
     * @param loudness Loudness parameter (0-100)
     * @param swingOverride Swing override parameter (0-100)
     * @return MidiBuffer containing generated drum events
     */
    juce::MidiBuffer generateRegion(int bars,
                                    double bpm,
                                    int styleIndex,
                                    const GrooveTemplate& groove,
                                    float complexity,
                                    float loudness,
                                    float swingOverride);

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

    // MIDI helpers
    void addNote(juce::MidiBuffer& buffer, int pitch, int velocity, int startTick, int durationTicks);
};
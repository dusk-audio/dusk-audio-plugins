#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <deque>

/**
 * Extracted groove information from MIDI input
 */
struct ExtractedGroove
{
    std::vector<double> noteOnTimes;     // Note-on times in seconds
    std::vector<int> velocities;          // Corresponding velocities
    std::vector<int> pitches;             // Note pitches
    int noteCount = 0;                    // Total notes analyzed
    double averageVelocity = 100.0;       // Mean velocity
    double velocityVariance = 0.0;        // Velocity consistency
    double noteDensity = 0.0;             // Notes per beat
};

/**
 * MidiGrooveExtractor - Extracts groove/timing information from incoming MIDI
 *
 * Analyzes note-on events to determine:
 * - Timing patterns (for swing detection)
 * - Velocity patterns (for dynamics)
 * - Note density (for energy estimation)
 */
class MidiGrooveExtractor
{
public:
    MidiGrooveExtractor();
    ~MidiGrooveExtractor() = default;

    /**
     * Prepare the extractor
     * @param sampleRate The audio sample rate
     */
    void prepare(double sampleRate);

    /**
     * Extract groove from a MIDI buffer
     * @param midiBuffer The MIDI buffer to analyze
     * @return Extracted groove information
     */
    ExtractedGroove extractFromBuffer(const juce::MidiBuffer& midiBuffer);

    /**
     * Add MIDI events to the analysis ring buffer
     * @param midiBuffer MIDI events to add
     * @param bufferStartTime Start time of this buffer in seconds
     */
    void addToRingBuffer(const juce::MidiBuffer& midiBuffer, double bufferStartTime);

    /**
     * Analyze the ring buffer and extract groove
     * @param bpm Current tempo for timing analysis
     * @return Extracted groove information
     */
    ExtractedGroove analyzeRingBuffer(double bpm);

    /**
     * Get note-on times from the last 2 seconds
     */
    std::vector<double> getRecentNoteOnTimes() const;

    /**
     * Get the number of notes in the analysis window
     */
    int getNoteCount() const { return static_cast<int>(noteRingBuffer.size()); }

    /**
     * Set minimum velocity threshold for analysis
     * @param velocity Minimum velocity (0-127)
     */
    void setVelocityThreshold(int velocity) { velocityThreshold = velocity; }

    /**
     * Reset the extractor state
     */
    void reset();

private:
    // Sample rate
    double sampleRate = 44100.0;

    // Note event for ring buffer
    struct NoteEvent
    {
        double timeSeconds;
        int pitch;
        int velocity;
    };

    // Ring buffer of note events (last 2 seconds)
    std::deque<NoteEvent> noteRingBuffer;
    static constexpr double BUFFER_DURATION = 2.0;  // 2 seconds

    // Analysis parameters
    int velocityThreshold = 60;  // Ignore notes below this velocity

    // Current buffer time tracking
    double currentTime = 0.0;

    // Helper methods
    void pruneOldEvents();
    double calculateAverageVelocity() const;
    double calculateVelocityVariance(double mean) const;
    double calculateNoteDensity(double bpm) const;
};
#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <array>
#include "MidiGrooveExtractor.h"

/**
 * GrooveTemplate - Captures the rhythmic feel of input audio/MIDI
 *
 * This is the core data structure that represents a "groove" or "feel"
 * extracted from input, which is then used to influence drum generation.
 */
struct GrooveTemplate
{
    // Swing amounts (0.0 = straight, 0.5 = triplet feel)
    float swing8 = 0.0f;              // Swing on 8th notes
    float swing16 = 0.0f;             // Swing on 16th notes

    // Micro-timing offsets per 32nd note position (in milliseconds)
    // Positive = late, Negative = early
    std::array<float, 32> microOffset = {0};

    // Dynamics
    float avgVelocity = 100.0f;       // Average velocity (0-127)
    float velocityRange = 20.0f;      // Velocity variation range

    // Energy and density
    float energy = 0.5f;              // Overall energy (0.0 - 1.0)
    float density = 0.5f;             // Note density (0.0 - 1.0)

    // Rhythmic characteristics
    int primaryDivision = 16;         // Primary subdivision (8 or 16)
    float syncopation = 0.0f;         // Amount of offbeat emphasis (0.0 - 1.0)

    // Accent pattern (emphasis on beat positions, normalized 0-1)
    std::array<float, 16> accentPattern = {
        1.0f, 0.3f, 0.5f, 0.3f,       // Beat 1
        0.8f, 0.3f, 0.5f, 0.3f,       // Beat 2
        0.9f, 0.3f, 0.5f, 0.3f,       // Beat 3
        0.8f, 0.3f, 0.5f, 0.3f        // Beat 4
    };

    // Validity
    int noteCount = 0;                // Number of notes used to generate this template
    bool isValid() const { return noteCount >= 4; }

    // Reset to defaults
    void reset()
    {
        swing8 = 0.0f;
        swing16 = 0.0f;
        microOffset.fill(0.0f);
        avgVelocity = 100.0f;
        velocityRange = 20.0f;
        energy = 0.5f;
        density = 0.5f;
        primaryDivision = 16;
        syncopation = 0.0f;
        accentPattern = {1.0f, 0.3f, 0.5f, 0.3f, 0.8f, 0.3f, 0.5f, 0.3f,
                        0.9f, 0.3f, 0.5f, 0.3f, 0.8f, 0.3f, 0.5f, 0.3f};
        noteCount = 0;
    }
};

/**
 * GrooveTemplateGenerator - Generates GrooveTemplates from audio/MIDI analysis
 *
 * Takes timing data from TransientDetector or MidiGrooveExtractor and
 * converts it into a GrooveTemplate that can be used by the DrummerEngine.
 */
class GrooveTemplateGenerator
{
public:
    GrooveTemplateGenerator();
    ~GrooveTemplateGenerator() = default;

    /**
     * Prepare the generator
     * @param sampleRate The audio sample rate
     */
    void prepare(double sampleRate);

    /**
     * Generate template from audio onset times
     * @param onsetTimes Vector of onset times in seconds
     * @param bpm Current tempo
     * @param sampleRate Audio sample rate
     * @return Generated groove template
     */
    GrooveTemplate generateFromOnsets(const std::vector<double>& onsetTimes,
                                      double bpm,
                                      double sampleRate);

    /**
     * Generate template from extracted MIDI groove
     * @param groove Extracted groove from MidiGrooveExtractor
     * @param bpm Current tempo
     * @return Generated groove template
     */
    GrooveTemplate generateFromMidi(const ExtractedGroove& groove, double bpm);

    /**
     * Merge two templates (for blending Follow sources)
     * @param a First template
     * @param b Second template
     * @param blend Blend factor (0.0 = all A, 1.0 = all B)
     * @return Blended template
     */
    static GrooveTemplate blend(const GrooveTemplate& a, const GrooveTemplate& b, float blendFactor);

    /**
     * Reset the generator state
     */
    void reset();

private:
    double sampleRate = 44100.0;

    // Helper methods
    int determinePrimaryDivision(const std::vector<double>& hitTimes, double bpm);
    float calculateSwing(const std::vector<double>& hitTimes, double bpm, int division);
    void calculateMicroOffsets(const std::vector<double>& hitTimes, double bpm,
                               std::array<float, 32>& offsets);
    float calculateSyncopation(const std::vector<double>& hitTimes, double bpm);
    void calculateAccentPattern(const std::vector<double>& hitTimes,
                                const std::vector<int>& velocities,
                                double bpm,
                                std::array<float, 16>& pattern);

    // Quantization helpers
    double quantizeToGrid(double timeSeconds, double bpm, int division);
    int getGridPosition(double timeSeconds, double bpm, int division);
    double getDeviationFromGrid(double timeSeconds, double bpm, int division);
};
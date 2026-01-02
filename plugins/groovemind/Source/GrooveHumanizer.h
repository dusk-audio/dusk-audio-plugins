/*
  ==============================================================================

    GrooveHumanizer.h
    Applies human-like timing and velocity variations to MIDI patterns

    Uses ML models trained on the Groove MIDI Dataset (Phase 3) to apply
    learned micro-timing and velocity patterns. Falls back to statistical
    humanization when ML model is not available.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "MLInference.h"

//==============================================================================
class GrooveHumanizer
{
public:
    GrooveHumanizer();
    ~GrooveHumanizer() = default;

    // Prepare for processing
    void prepare(double sampleRate);

    // Apply humanization to a MIDI buffer
    void process(juce::MidiBuffer& midiBuffer, double bpm);

    // Parameters
    void setGrooveAmount(float amount);  // 0-1, how much humanization to apply
    void setSwing(float swing);          // 0-1, swing amount
    void setTimingVariation(float ms);   // Max timing offset in ms
    void setVelocityVariation(float amount);  // 0-1, velocity variation amount

    // Style-specific presets
    void setGroovePreset(int preset);    // 0=tight, 1=relaxed, 2=jazzy, 3=behind

    // ML model loading
    bool loadModel(const juce::File& modelFile);
    bool loadTimingStats(const juce::File& statsFile);
    bool isModelLoaded() const { return mlModelLoaded; }
    bool useMLInference() const { return mlModelLoaded && useML; }

    // Enable/disable ML inference (for A/B testing)
    void setUseML(bool shouldUseML) { useML = shouldUseML; }

private:
    double sampleRate = 44100.0;

    // Parameters
    float grooveAmount = 0.7f;
    float swing = 0.0f;
    float timingVariationMs = 15.0f;
    float velocityVariation = 0.2f;

    // Push/pull feel
    float pushPull = 0.0f;  // -1 = laid back, +1 = pushing

    // ML model
    MLInference::HumanizerModel humanizerModel;
    MLInference::TimingStatsLibrary timingStats;
    bool mlModelLoaded = false;
    bool useML = true;

    // Random for variation
    mutable juce::Random random;

    // Per-instrument timing adjustments (learned from GMD data)
    struct InstrumentTiming
    {
        float offsetMs = 0.0f;      // Base timing offset
        float variationMs = 10.0f;  // Random variation
        float velocityScale = 1.0f; // Velocity multiplier
    };

    std::map<int, InstrumentTiming> instrumentTimings;

    // Note to instrument category mapping
    int noteToCategory(int midiNote) const;

    // Initialize default timings from GMD statistics
    void initializeDefaultTimings();

    // Calculate timing offset for a note
    float calculateTimingOffset(int noteNumber, double beatPosition) const;

    // Calculate timing offset using ML model
    float calculateMLTimingOffset(int noteNumber, double beatPosition, int velocity,
                                   int prevNote, int nextNote) const;

    // Calculate velocity adjustment
    int adjustVelocity(int originalVelocity, int noteNumber, double beatPosition) const;

    // Apply swing to beat position
    double applySwing(double beatPosition) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveHumanizer)
};

/*
  ==============================================================================

    GrooveHumanizer.h
    Applies human-like timing and velocity variations to MIDI patterns

    In the future, this will use ML models trained on the Groove MIDI Dataset
    to apply learned micro-timing and velocity patterns. For now, it uses
    algorithmic humanization.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

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

    // Future: ML model loading
    bool loadModel(const juce::File& modelFile);
    bool isModelLoaded() const { return mlModelLoaded; }

private:
    double sampleRate = 44100.0;

    // Parameters
    float grooveAmount = 0.7f;
    float swing = 0.0f;
    float timingVariationMs = 15.0f;
    float velocityVariation = 0.2f;

    // Push/pull feel
    float pushPull = 0.0f;  // -1 = laid back, +1 = pushing

    // ML model state (future)
    bool mlModelLoaded = false;
    // RTNeural model will go here

    // Random for variation
    mutable juce::Random random;

    // Per-instrument timing adjustments (future: learned from data)
    struct InstrumentTiming
    {
        float offsetMs = 0.0f;      // Base timing offset
        float variationMs = 10.0f;  // Random variation
        float velocityScale = 1.0f; // Velocity multiplier
    };

    std::map<int, InstrumentTiming> instrumentTimings;

    // Initialize default timings
    void initializeDefaultTimings();

    // Calculate timing offset for a note
    float calculateTimingOffset(int noteNumber, double beatPosition) const;

    // Calculate velocity adjustment
    int adjustVelocity(int originalVelocity, int noteNumber, double beatPosition) const;

    // Apply swing to beat position
    double applySwing(double beatPosition) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveHumanizer)
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <bitset>

/**
 * StepSequencer - 16-step visual grid for drum pattern input
 *
 * Features:
 * - 16 steps per bar
 * - Multiple drum lanes (kick, snare, hi-hat, etc.)
 * - Click to toggle steps
 * - Visual feedback for active steps
 * - Velocity support (click and drag up/down)
 */
class StepSequencer : public juce::Component,
                      public juce::Timer
{
public:
    // Drum lanes in the sequencer
    enum DrumLane
    {
        Kick = 0,
        Snare,
        ClosedHiHat,
        OpenHiHat,
        Clap,
        Tom1,
        Tom2,
        Crash,
        NumLanes
    };

    // Step data structure
    struct Step
    {
        bool active = false;
        float velocity = 0.8f;  // 0.0 - 1.0
    };

    StepSequencer();
    ~StepSequencer() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Timer for playhead animation
    void timerCallback() override;

    // Set current playhead position (0-15)
    void setPlayheadPosition(int step);

    // Get step data
    bool isStepActive(int lane, int step) const;
    float getStepVelocity(int lane, int step) const;

    // Set step data
    void setStep(int lane, int step, bool active, float velocity = 0.8f);
    void clearAllSteps();

    // Pattern callback when steps change
    std::function<void()> onPatternChanged;

    // Get the full pattern for MIDI generation
    const std::array<std::array<Step, 16>, NumLanes>& getPattern() const { return pattern; }

private:
    // 16 steps x NumLanes pattern grid
    std::array<std::array<Step, 16>, NumLanes> pattern;

    // Current playhead position
    int currentStep = -1;

    // Drag state
    bool isDragging = false;
    int dragLane = -1;
    int dragStep = -1;
    float dragStartY = 0.0f;
    float dragStartVelocity = 0.0f;

    // Visual constants
    static constexpr int stepWidth = 24;
    static constexpr int laneHeight = 20;
    static constexpr int labelWidth = 60;
    static constexpr int headerHeight = 20;

    // Lane labels
    static constexpr const char* laneNames[NumLanes] = {
        "Kick", "Snare", "HH Cls", "HH Opn", "Clap", "Tom 1", "Tom 2", "Crash"
    };

    // Lane colors
    juce::Colour getLaneColor(int lane) const;

    // Get step/lane from mouse position
    std::pair<int, int> getStepLaneFromPosition(juce::Point<int> pos) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencer)
};

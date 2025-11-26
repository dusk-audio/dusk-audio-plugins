#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

/**
 * FollowModePanel - UI panel for Follow Mode controls
 *
 * Displays:
 * - Follow Mode enable toggle
 * - Source selection (MIDI/Audio)
 * - Sensitivity slider
 * - Groove lock indicator
 * - Mini waveform/activity display
 */
class FollowModePanel : public juce::Component
{
public:
    FollowModePanel(DrummerCloneAudioProcessor& processor);
    ~FollowModePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Update display (call from timer)
    void updateDisplay();

private:
    DrummerCloneAudioProcessor& audioProcessor;

    // Controls
    juce::ToggleButton enableToggle;
    juce::ComboBox sourceComboBox;
    juce::Label sourceLabel;
    juce::Slider sensitivitySlider;
    juce::Label sensitivityLabel;
    juce::Label instructionLabel;  // Help text explaining how to use Follow Mode

    // Groove lock display
    juce::Label lockLabel;
    float currentLockPercentage = 0.0f;

    // Activity LED
    bool activityState = false;
    int activityCounter = 0;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensitivityAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FollowModePanel)
};
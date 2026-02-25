#pragma once

#include "PluginProcessor.h"
#include "LEDMeter.h"
#include "ScalableEditorHelper.h"
#include "UserPresetManager.h"
#include "SupportersOverlay.h"

class DuskVerbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DuskVerbLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    static constexpr juce::uint32 kBackground   = 0xff1a1a2e;
    static constexpr juce::uint32 kPanel        = 0xff16213e;
    static constexpr juce::uint32 kAccent       = 0xffe94560;
    static constexpr juce::uint32 kKnobFill     = 0xff0f3460;
    static constexpr juce::uint32 kText         = 0xffe0e0e0;
    static constexpr juce::uint32 kSubtleText   = 0xff888888;
    static constexpr juce::uint32 kFreezeOn     = 0xff00c8ff;
    static constexpr juce::uint32 kBorder       = 0xff353560;
};

struct KnobWithLabel
{
    juce::Slider slider;
    juce::Label  nameLabel;
    juce::Label  valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    void init (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramID, const juce::String& displayName,
               const juce::String& suffix, const juce::String& tooltip = {});
};

// 5-segment horizontal button strip for algorithm selection
class AlgorithmSelector : public juce::Component
{
public:
    AlgorithmSelector (juce::RangedAudioParameter& param);

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::RangedAudioParameter& param_;
    juce::ParameterAttachment attachment_;
    int currentIndex_ = 1;
    juce::StringArray labels_ { "Plate", "Hall", "Chamber", "Room", "Ambient" };
    std::vector<juce::Rectangle<int>> segmentBounds_;
};

class DuskVerbEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit DuskVerbEditor (DuskVerbProcessor&);
    ~DuskVerbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showSupportersPanel();
    void hideSupportersPanel();

    DuskVerbProcessor& processorRef;
    DuskVerbLookAndFeel lnf_;
    ScalableEditorHelper scaler_;

    // Algorithm selector (segmented button strip)
    std::unique_ptr<AlgorithmSelector> algorithmSelector_;

    // Preset browser
    juce::ComboBox presetBox_;
    void loadPreset (int index);
    void refreshPresetList();

    // User preset management
    std::unique_ptr<UserPresetManager> userPresetManager_;
    juce::TextButton savePresetButton_;
    juce::TextButton deletePresetButton_;
    void saveUserPreset();
    void loadUserPreset (const juce::String& name);
    void deleteUserPreset (const juce::String& name);
    void updateDeleteButtonVisibility();

    // Knobs
    KnobWithLabel preDelay_;
    KnobWithLabel diffusion_;
    KnobWithLabel decay_;
    KnobWithLabel size_;
    KnobWithLabel bassMult_;
    KnobWithLabel trebleMult_;
    KnobWithLabel crossover_;
    KnobWithLabel modDepth_;
    KnobWithLabel modRate_;
    KnobWithLabel erLevel_;
    KnobWithLabel erSize_;
    KnobWithLabel mix_;
    KnobWithLabel loCut_;
    KnobWithLabel hiCut_;
    KnobWithLabel width_;

    // Freeze toggle (inside TIME group)
    juce::ToggleButton freezeButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment_;

    // Bus mode toggle (inside OUTPUT group)
    juce::ToggleButton busModeButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> busModeAttachment_;

    // Pre-delay sync (inside INPUT group)
    juce::ComboBox predelaySyncBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> predelaySyncAttachment_;

    // Level meters
    LEDMeter inputMeter_  { LEDMeter::Vertical };
    LEDMeter outputMeter_ { LEDMeter::Vertical };

    // Supporters overlay
    std::unique_ptr<SupportersOverlay> supportersOverlay_;
    juce::Rectangle<int> titleClickArea_;

    // Tooltip window (required for setTooltip to display in plugin editors)
    juce::TooltipWindow tooltipWindow_ { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskVerbEditor)
};

/*
  ==============================================================================

    PluginEditor.h
    Suede 200 — Vintage Digital Reverberator

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "../../shared/DuskLookAndFeel.h"
#include "../../shared/ScalableEditorHelper.h"
#include "../../shared/LEDMeter.h"
#include "../../shared/SupportersOverlay.h"

//==============================================================================
// Custom look and feel for the Suede 200 aesthetic
class Suede200LookAndFeel : public DuskLookAndFeel
{
public:
    Suede200LookAndFeel();
    ~Suede200LookAndFeel() override = default;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Three-position selector (matching the original's 3-LED button groups)
class ThreeWaySelector : public juce::Component
{
public:
    ThreeWaySelector(const juce::String& label,
                     const juce::StringArray& options);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

    void setSelectedIndex(int index);
    int getSelectedIndex() const { return selectedIndex; }

    std::function<void(int)> onChange;

private:
    juce::String labelText;
    juce::StringArray optionLabels;
    int selectedIndex = 1; // default: middle
};

//==============================================================================
// Preset browser overlay
class Suede200PresetBrowser : public juce::Component
{
public:
    explicit Suede200PresetBrowser(Suede200Processor& p);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    std::function<void()> onDismiss;

private:
    Suede200Processor& processor;
    juce::String selectedCategory;
    std::vector<juce::String> categoryOrder;
};

//==============================================================================
class Suede200Editor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit Suede200Editor(Suede200Processor&);
    ~Suede200Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    Suede200Processor& audioProcessor;
    Suede200LookAndFeel lookAndFeel;

    ScalableEditorHelper resizeHelper;

    // Program selector buttons (6 programs)
    juce::ToggleButton concertHallButton;
    juce::ToggleButton plateButton;
    juce::ToggleButton chamberButton;
    juce::ToggleButton richPlateButton;
    juce::ToggleButton richSplitsButton;
    juce::ToggleButton inverseRoomsButton;

    // Main knobs
    DuskSlider predelaySlider;
    DuskSlider reverbTimeSlider;
    DuskSlider sizeSlider;
    DuskSlider mixSlider;

    // Pre-Echoes toggle
    juce::ToggleButton preEchoesButton;

    // 3-way selectors
    ThreeWaySelector diffusionSelector;
    ThreeWaySelector rtLowSelector;
    ThreeWaySelector rtHighSelector;
    ThreeWaySelector rolloffSelector;

    // Labels
    juce::Label predelayLabel;
    juce::Label reverbTimeLabel;
    juce::Label sizeLabel;
    juce::Label mixLabel;

    // LED output meter
    LEDMeter outputMeter { LEDMeter::Vertical };

    // Preset browser
    std::unique_ptr<Suede200PresetBrowser> presetBrowser;
    juce::TextButton prevPresetButton;
    juce::TextButton nextPresetButton;
    juce::Label presetNameLabel;

    // Supporters overlay
    std::unique_ptr<SupportersOverlay> supportersOverlay;
    juce::Rectangle<int> titleClickArea;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> predelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbTimeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> preEchoesAttachment;

    // Program and discrete parameter handling
    void updateProgramButtons();
    void programButtonClicked(int program);
    void updateDiscreteParams();
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void showSupportersPanel();
    void showPresetBrowser();
    void navigatePreset(int delta);
    void updatePresetDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Suede200Editor)
};

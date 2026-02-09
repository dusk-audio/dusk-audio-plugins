/*
  ==============================================================================

    PluginEditor.h
    SilkVerb - Algorithmic Reverb with Plate, Room, Hall modes

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "../../shared/LunaLookAndFeel.h"
#include "../../shared/ScalableEditorHelper.h"
#include "../../shared/LEDMeter.h"
#include "../../shared/SupportersOverlay.h"

//==============================================================================
// Custom look and feel for SilkVerb matching Luna plugin style
class SilkVerbLookAndFeel : public LunaLookAndFeel
{
public:
    SilkVerbLookAndFeel();
    ~SilkVerbLookAndFeel() override = default;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void setFreezeButton(juce::ToggleButton* button) { freezeButtonPtr = button; }

private:
    juce::ToggleButton* freezeButtonPtr = nullptr;
};

//==============================================================================
// PCM 90-inspired VFD display — green phosphor text on dark background
class LCDDisplay : public juce::Component
{
public:
    LCDDisplay();

    void setLine1(const juce::String& text) { line1 = text; repaint(); }
    void setLine1Right(const juce::String& text) { line1Right = text; repaint(); }
    void setLine2(const juce::String& text) { line2 = text; repaint(); }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;

    std::function<void()> onClick;

private:
    juce::String line1;
    juce::String line1Right;
    juce::String line2;
};

//==============================================================================
// Preset browser overlay — category-tabbed popup for browsing presets
class PresetBrowserOverlay : public juce::Component
{
public:
    explicit PresetBrowserOverlay(SilkVerbProcessor& p);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    std::function<void()> onDismiss;

private:
    SilkVerbProcessor& processor;
    juce::String selectedCategory;
    std::vector<juce::String> categoryOrder;
};

//==============================================================================
class SilkVerbEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit SilkVerbEditor(SilkVerbProcessor&);
    ~SilkVerbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    SilkVerbProcessor& audioProcessor;
    SilkVerbLookAndFeel lookAndFeel;

    // Resizable UI helper (shared across all Luna plugins)
    ScalableEditorHelper resizeHelper;

    // Mode toggle buttons (Row 1: Plate/Room/Hall/BrHall/Chamber, Row 2: Cathedral/Ambience/Chorus/Random/Dirty)
    juce::ToggleButton plateButton;
    juce::ToggleButton roomButton;
    juce::ToggleButton hallButton;
    juce::ToggleButton brightHallButton;
    juce::ToggleButton chamberButton;
    juce::ToggleButton cathedralButton;
    juce::ToggleButton ambienceButton;
    juce::ToggleButton chorusButton;
    juce::ToggleButton randomButton;
    juce::ToggleButton dirtyButton;

    // Freeze toggle button
    juce::ToggleButton freezeButton;

    // Pre-delay tempo sync controls
    juce::ToggleButton preDelaySyncButton;
    juce::ComboBox preDelayNoteBox;

    // Row 1 — Reverb character (Size, Pre-Delay, Shape, Spread)
    LunaSlider sizeSlider;
    LunaSlider preDelaySlider;
    LunaSlider shapeSlider;
    LunaSlider spreadSlider;

    // Row 2 — Tone (Damping, Bass Boost, HF Decay, Diffusion)
    LunaSlider dampingSlider;
    LunaSlider bassBoostSlider;
    LunaSlider hfDecaySlider;
    LunaSlider diffusionSlider;

    // Row 3 — Output (Width, Mix, Low Cut, High Cut)
    LunaSlider widthSlider;
    LunaSlider mixSlider;
    LunaSlider lowCutSlider;
    LunaSlider highCutSlider;

    // LED output meter
    LEDMeter outputMeter { LEDMeter::Vertical };

    // Preset browser with PCM 90-style LCD
    std::unique_ptr<PresetBrowserOverlay> presetBrowser;
    LCDDisplay lcdDisplay;
    juce::TextButton prevPresetButton;
    juce::TextButton nextPresetButton;

    // Supporters overlay
    std::unique_ptr<SupportersOverlay> supportersOverlay;
    juce::Rectangle<int> titleClickArea;

    // Labels
    juce::Label sizeLabel;
    juce::Label preDelayLabel;
    juce::Label shapeLabel;
    juce::Label spreadLabel;
    juce::Label dampingLabel;
    juce::Label bassBoostLabel;
    juce::Label hfDecayLabel;
    juce::Label diffusionLabel;
    juce::Label widthLabel;
    juce::Label mixLabel;
    juce::Label lowCutLabel;
    juce::Label highCutLabel;

    // Attachments - Row 1 (Reverb)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> shapeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spreadAttachment;

    // Attachments - Row 2 (Tone)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassBoostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hfDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionAttachment;

    // Attachments - Row 3 (Output)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;

    // Attachment - Freeze
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    // Attachments - Pre-delay sync
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> preDelaySyncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> preDelayNoteAttachment;

    // Mode button group handling
    void updateModeButtons();
    void modeButtonClicked(int mode);

    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void showSupportersPanel();
    void showPresetBrowser();
    void navigatePreset(int delta);
    void updatePresetDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilkVerbEditor)
};

/*
  ==============================================================================

    PluginEditor.h
    Velvet 90 - Algorithmic Reverb with Plate, Room, Hall modes

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
// Custom look and feel for Velvet 90 matching Dusk Audio plugin style
class Velvet90LookAndFeel : public DuskLookAndFeel
{
public:
    Velvet90LookAndFeel();
    ~Velvet90LookAndFeel() override = default;

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
    explicit PresetBrowserOverlay(Velvet90Processor& p);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    std::function<void()> onDismiss;

private:
    Velvet90Processor& processor;
    juce::String selectedCategory;
    std::vector<juce::String> categoryOrder;
};

//==============================================================================
class Velvet90Editor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit Velvet90Editor(Velvet90Processor&);
    ~Velvet90Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    Velvet90Processor& audioProcessor;
    Velvet90LookAndFeel lookAndFeel;

    // Resizable UI helper (shared across all Dusk Audio plugins)
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

    // Tab state
    int currentTab = 0;

    // === Tab 0: MAIN ===
    // Row 1 — Reverb character (Size, Pre-Delay, Shape, Spread)
    DuskSlider sizeSlider;
    DuskSlider preDelaySlider;
    DuskSlider shapeSlider;
    DuskSlider spreadSlider;

    // Row 2 — Tone (Damping, Bass Boost, HF Decay, Diffusion)
    DuskSlider dampingSlider;
    DuskSlider bassBoostSlider;
    DuskSlider hfDecaySlider;
    DuskSlider diffusionSlider;

    // Row 3 — Output (Width, Mix, Low Cut, High Cut)
    DuskSlider widthSlider;
    DuskSlider mixSlider;
    DuskSlider lowCutSlider;
    DuskSlider highCutSlider;

    // === Tab 1: DECAY ===
    // Row 1 — Room (Room Size, Early Diff, ER/Late, ER Bass Cut)
    DuskSlider roomSizeSlider;
    DuskSlider earlyDiffSlider;
    DuskSlider erLateBalSlider;
    DuskSlider erBassCutSlider;
    // Row 2 — Frequency (Bass Freq, Mid Decay, High Freq, Treble Ratio)
    DuskSlider bassFreqSlider;
    DuskSlider midDecaySlider;
    DuskSlider highFreqSlider;
    DuskSlider trebleRatioSlider;
    // Row 3 — Modulation (Low-Mid Freq, Low-Mid Decay, Mod Rate, Mod Depth)
    DuskSlider lowMidFreqSlider;
    DuskSlider lowMidDecaySlider;
    DuskSlider modRateSlider;
    DuskSlider modDepthSlider;

    // === Tab 2: EFFECTS ===
    // Row 1 — Envelope (Mode combo, Depth, Hold, Release)
    juce::ComboBox envModeBox;
    DuskSlider envDepthSlider;
    DuskSlider envHoldSlider;
    DuskSlider envReleaseSlider;
    // Row 2 — Echo (Delay, Feedback, Ping-Pong, Resonance)
    DuskSlider echoDelaySlider;
    DuskSlider echoFeedbackSlider;
    DuskSlider echoPingPongSlider;
    DuskSlider resonanceSlider;
    // Row 3 — Dynamics (Amount, Speed, Stereo Coupling, Stereo Invert)
    DuskSlider dynAmountSlider;
    DuskSlider dynSpeedSlider;
    DuskSlider stereoCouplingSlider;
    DuskSlider stereoInvertSlider;

    // === Tab 3: OUTPUT EQ ===
    // Row 1 — Band 1 (Freq, Gain, Q)
    DuskSlider outEQ1FreqSlider;
    DuskSlider outEQ1GainSlider;
    DuskSlider outEQ1QSlider;
    // Row 2 — Band 2 (Freq, Gain, Q)
    DuskSlider outEQ2FreqSlider;
    DuskSlider outEQ2GainSlider;
    DuskSlider outEQ2QSlider;

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

    // Labels — Tab 0 (MAIN)
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

    // Labels — Tab 1 (DECAY)
    juce::Label roomSizeLabel;
    juce::Label earlyDiffLabel;
    juce::Label erLateBalLabel;
    juce::Label erBassCutLabel;
    juce::Label bassFreqLabel;
    juce::Label midDecayLabel;
    juce::Label highFreqLabel;
    juce::Label trebleRatioLabel;
    juce::Label lowMidFreqLabel;
    juce::Label lowMidDecayLabel;
    juce::Label modRateLabel;
    juce::Label modDepthLabel;

    // Labels — Tab 2 (EFFECTS)
    juce::Label envModeLabel;
    juce::Label envDepthLabel;
    juce::Label envHoldLabel;
    juce::Label envReleaseLabel;
    juce::Label echoDelayLabel;
    juce::Label echoFeedbackLabel;
    juce::Label echoPingPongLabel;
    juce::Label resonanceLabel;
    juce::Label dynAmountLabel;
    juce::Label dynSpeedLabel;
    juce::Label stereoCouplingLabel;
    juce::Label stereoInvertLabel;

    // Labels — Tab 3 (OUTPUT EQ)
    juce::Label outEQ1FreqLabel;
    juce::Label outEQ1GainLabel;
    juce::Label outEQ1QLabel;
    juce::Label outEQ2FreqLabel;
    juce::Label outEQ2GainLabel;
    juce::Label outEQ2QLabel;

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

    // Attachments — Tab 1 (DECAY)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> roomSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> earlyDiffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> erLateBalAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> erBassCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> midDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trebleRatioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowMidFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowMidDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modDepthAttachment;

    // Attachments — Tab 2 (EFFECTS)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> envModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> envDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> envHoldAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> envReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> echoDelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> echoFeedbackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> echoPingPongAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> resonanceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dynAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dynSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stereoCouplingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stereoInvertAttachment;

    // Attachments — Tab 3 (OUTPUT EQ)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outEQ1FreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outEQ1GainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outEQ1QAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outEQ2FreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outEQ2GainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outEQ2QAttachment;

    // Attachment - Freeze
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    // Attachments - Pre-delay sync
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> preDelaySyncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> preDelayNoteAttachment;

    // Mode button group handling
    void updateModeButtons();
    void modeButtonClicked(int mode);

    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupLabel(juce::Label& label, const juce::String& text);
    void switchTab(int tab);
    void showSupportersPanel();
    void showPresetBrowser();
    void navigatePreset(int delta);
    void updatePresetDisplay();

    // Tab bar hit area (stored for mouseDown)
    juce::Rectangle<int> tabBarArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Velvet90Editor)
};

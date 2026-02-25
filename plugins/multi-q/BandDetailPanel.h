#pragma once

#include <JuceHeader.h>
#include "EQBand.h"

class MultiQ;

/**
    BandDetailPanel - Waves F6 style bottom section

    Layout (145px total height):
    - Band selector row at top (8 colored pill buttons, 32px height)
    - Colored accent line below selector matching selected band
    - Large rotary knobs with labels above and values with units below
      FREQ | Q | GAIN | THRESHOLD | ATTACK | RELEASE | RANGE | RATIO | [DYN] [SOLO]

    Click band nodes in the graphic display or selector buttons to select bands.
    Dynamics controls dim when DYN is off.
    EQ knob arcs reflect selected band color.
*/
class BandDetailPanel : public juce::Component,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit BandDetailPanel(MultiQ& processor);
    ~BandDetailPanel() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;  // Draw value text on top of knobs
    void resized() override;

    // Mouse handling for band selector
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Selection management
    void setSelectedBand(int bandIndex);
    int getSelectedBand() const { return selectedBand.load(); }

    // Match mode — replaces dynamics section with match controls
    void setMatchMode(bool isMatch);

    // Callbacks
    std::function<void(int)> onBandSelected;
    std::function<void(int, bool)> onBandEnableChanged;

private:
    MultiQ& processor;
    std::atomic<int> selectedBand{0};  // Start with band 1 selected (atomic: read by audio thread via parameterChanged)
    int hoveredBand = -1;   // For hover effects on band selector

    // Band selector (drawn manually, no TextButtons)
    void setupBandButtons();  // Legacy - no longer used
    void updateBandButtonColors();
    juce::Rectangle<int> getBandButtonBounds() const;

    // Main EQ controls (large rotary knobs)
    std::unique_ptr<juce::Slider> freqKnob;
    std::unique_ptr<juce::Slider> gainKnob;
    std::unique_ptr<juce::Slider> qKnob;
    std::unique_ptr<juce::ComboBox> slopeSelector;  // For HPF/LPF

    // Dynamics controls (large rotary knobs)
    std::unique_ptr<juce::Slider> thresholdKnob;
    std::unique_ptr<juce::Slider> attackKnob;
    std::unique_ptr<juce::Slider> releaseKnob;
    std::unique_ptr<juce::Slider> rangeKnob;
    std::unique_ptr<juce::Slider> ratioKnob;

    // Toggle buttons
    std::unique_ptr<juce::TextButton> dynButton;
    std::unique_ptr<juce::TextButton> soloButton;

    // Parameter attachments (recreated when band changes)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> qAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> slopeAttachment;

    // Dynamics attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dynEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> threshAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;

    // Match mode state and controls (shown instead of dynamics)
    bool matchMode = false;
    juce::TextButton matchCaptureRefButton{"Capture Ref"};
    juce::TextButton matchCaptureSrcButton{"Capture Source"};
    juce::TextButton matchComputeButton{"Match"};
    juce::TextButton matchClearButton{"Clear"};
    std::unique_ptr<juce::Slider> matchStrengthSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> matchStrengthAttachment;

    // Setup methods
    void setupKnobs();
    void setupMatchControls();
    void updateAttachments();
    void updateControlsForBandType();
    void updateDynamicsOpacity();

    // Drawing helpers
    void drawKnobWithLabel(juce::Graphics& g, juce::Slider* knob,
                           const juce::String& label, const juce::String& value,
                           juce::Rectangle<int> bounds, bool dimmed = false);

    // Helpers
    juce::Colour getBandColor(int bandIndex) const;
    BandType getBandType(int bandIndex) const;
    bool isDynamicsEnabled() const;

    // Parameter listener
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandDetailPanel)
};

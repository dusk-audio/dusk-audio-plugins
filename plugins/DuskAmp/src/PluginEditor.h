#pragma once

#include "PluginProcessor.h"
#include "ui/DuskAmpLookAndFeel.h"
#include "ui/CabBrowser.h"
#include "ui/NAMBrowser.h"
#include "ui/TunerOverlay.h"
#include "LEDMeter.h"
#include "ScalableEditorHelper.h"
#include "UserPresetManager.h"
#include "SupportersOverlay.h"

// Reusable knob+label (same pattern as DuskVerb)
struct KnobWithLabel
{
    juce::Slider slider;
    juce::Label  nameLabel;
    juce::Label  valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String boundParamID_;
    juce::String suffix_;

    void init (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramID, const juce::String& displayName,
               const juce::String& suffix, const juce::String& tooltip = {});

    // Re-bind the slider to a different APVTS parameter (used to swap
    // bass/mid/treble between DSP and NAM tonestack params on mode change).
    void rebind (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID);

    // Re-installs the unit-aware formatter on slider.textFromValueFunction.
    // Must be called after every SliderAttachment (re)creation — the
    // attachment's constructor wipes any prior textFromValueFunction.
    void applyFormatter();

    void setDimmed (bool dimmed)
    {
        float alpha = dimmed ? 0.4f : 1.0f;
        slider.setAlpha (alpha);
        nameLabel.setAlpha (alpha);
        valueLabel.setAlpha (alpha);
    }
};

// Single-segment momentary "pill" button — matches the AmpModeSelector
// visual language (rounded panel, accent fill when active/hovered) so the
// TUNER trigger reads as part of the same control group instead of a
// generic TextButton.
class PillButton : public juce::Button
{
public:
    explicit PillButton (const juce::String& text) : juce::Button (text) {}

    /** When true, the pill is painted as if selected (accent fill) regardless
        of mouse state. Used to show the tuner is currently active. */
    void setActive (bool active) { active_ = active; repaint(); }
    bool isActive() const noexcept { return active_; }

protected:
    void paintButton (juce::Graphics& g,
                      bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

private:
    bool active_ = false;
};

// A/B snapshot pill — left-click recalls the slot (or captures if empty),
// right-click always captures the current state into the slot. Pure UI; the
// processor owns the actual state.
class ABPillButton : public PillButton
{
public:
    using PillButton::PillButton;
    std::function<void()> onCapture;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onCapture) onCapture();
            return;
        }
        PillButton::mouseDown (e);
    }
};

// 2-segment mode selector (DSP / NAM)
class AmpModeSelector : public juce::Component
{
public:
    AmpModeSelector (juce::RangedAudioParameter& param);

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::RangedAudioParameter& param_;
    juce::ParameterAttachment attachment_;
    int currentIndex_ = 0;
    juce::StringArray labels_ { "DSP", "NAM" };
    std::vector<juce::Rectangle<int>> segmentBounds_;
};

class DuskAmpEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit DuskAmpEditor (DuskAmpProcessor&);
    ~DuskAmpEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showSupportersPanel();
    void hideSupportersPanel();
    DuskAmpProcessor& processorRef;
    DuskAmpLookAndFeel lnf_;
    ScalableEditorHelper scaler_;

    // Mode selector
    std::unique_ptr<AmpModeSelector> modeSelector_;

    // Tuner — button in the top bar opens a full-editor modal that mutes
    // the audio. Detection runs continuously on the audio thread; the
    // overlay polls via the same timer that drives knob value labels.
    PillButton tunerButton_ { "TUNER" };

    // A/B snapshot pills — session-only state slots. Left-click recalls
    // (captures-on-first-click if empty); right-click forces capture.
    ABPillButton slotAButton_ { "A" };
    ABPillButton slotBButton_ { "B" };

    std::unique_ptr<TunerOverlay> tunerOverlay_;
    // Owns the APVTS binding so the reference-Hz slider inside the overlay
    // round-trips on session save/load. Lifetime tied to tunerOverlay_.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tunerRefHzAttachment_;
    void showTunerPanel();
    void hideTunerPanel();

    // Preset browser
    juce::ComboBox presetBox_;
    std::unique_ptr<UserPresetManager> userPresetManager_;
    juce::TextButton savePresetButton_;
    juce::TextButton deletePresetButton_;
    void saveUserPreset();
    void loadUserPreset (const juce::String& name);
    void deleteUserPreset (const juce::String& name);
    void refreshPresetList();
    void updateDeleteButtonVisibility();

    // Snapshot of all APVTS param values at the moment the current preset
    // was loaded. The timer compares the live state against this each tick;
    // any divergence flips presetDirty_ on and the "*" indicator next to
    // the preset combo lights up.
    std::vector<float> presetSnapshot_;
    bool presetDirty_ = false;
    juce::Label presetDirtyLabel_;
    void capturePresetSnapshot();
    bool currentStateMatchesSnapshot() const;

    // -- INPUT section --
    KnobWithLabel inputGain_;
    KnobWithLabel gateThreshold_;
    KnobWithLabel gateRelease_;

    // -- AMP section --
    KnobWithLabel preampGain_;
    // Channel selector
    juce::ComboBox channelBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelAttachment_;
    // Bright toggle
    juce::ToggleButton brightButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> brightAttachment_;

    // -- TONE section --
    KnobWithLabel bass_;
    KnobWithLabel mid_;
    KnobWithLabel treble_;
    juce::ComboBox toneTypeBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> toneTypeAttachment_;
    int  toneTypeBoundMode_ = -1; // 0 = DSP, 1 = NAM; tracks which APVTS param toneTypeBox is bound to

    void rebindToneStackForMode (int ampMode); // 0 = DSP, 1 = NAM

    // -- POWER AMP section --
    KnobWithLabel powerDrive_;
    KnobWithLabel presence_;
    KnobWithLabel resonance_;
    KnobWithLabel sag_;

    // -- CABINET section --
    juce::ToggleButton cabEnabled_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabEnabledAttachment_;
    juce::ToggleButton cabNormalize_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabNormalizeAttachment_;
    KnobWithLabel cabMix_;
    KnobWithLabel cabHiCut_;
    KnobWithLabel cabLoCut_;
    CabBrowser cabBrowser_;

    // -- EFFECTS section --
    juce::ToggleButton delayEnabled_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> delayEnabledAttachment_;
    KnobWithLabel delayTime_;
    KnobWithLabel delayFeedback_;
    KnobWithLabel delayMix_;
    juce::ToggleButton reverbEnabled_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverbEnabledAttachment_;
    KnobWithLabel reverbMix_;
    KnobWithLabel reverbDecay_;

    // -- NAM browser --
    NAMBrowser namBrowser_;

    // -- OUTPUT --
    KnobWithLabel outputLevel_;

    // Oversampling selector
    juce::ComboBox oversamplingBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment_;

    // Level meters
    LEDMeter inputMeter_  { LEDMeter::Vertical };
    LEDMeter outputMeter_ { LEDMeter::Vertical };

    // Supporters
    std::unique_ptr<SupportersOverlay> supportersOverlay_;
    juce::Rectangle<int> titleClickArea_;

    // Row bounds — the full horizontal slot per row (used for rotated
    // right-edge labels). Each row is further split into sub-cards drawn
    // as separate metallic panels.
    juce::Rectangle<int> row1Bounds_, row2Bounds_, row3Bounds_;

    // Sub-card bounds — each is its own metallic panel.
    //   Row 1 (AMP / NAM):     [AMP] [TONE]
    //   Row 2 (POWER + CAB):   [POWER] [CAB]
    //   Row 3 (FX + OUT):      [DELAY] [REVERB] [OUTPUT]
    juce::Rectangle<int> row1LeftCard_, row1RightCard_;
    juce::Rectangle<int> row2LeftCard_, row2RightCard_;
    juce::Rectangle<int> row3DelayCard_, row3ReverbCard_, row3OutputCard_;

    // Left BROWSERS sidebar bounds. NAM mode = both sub-panels visible;
    // DSP mode = cab browser fills the whole sidebar (no list of DSP
    // voicings exists — selection is via inline Channel▼).
    juce::Rectangle<int> sidebarBounds_;
    juce::Rectangle<int> sidebarAmpListBounds_;
    juce::Rectangle<int> sidebarCabListBounds_;

    bool layoutIsNamMode_ = false;

    // Tooltip
    juce::TooltipWindow tooltipWindow_ { this, 500 };

    // Footer strip — version left, hover-tooltip mirror centre, CPU right.
    // Tooltip mirror updates from the timer by reading the component the
    // mouse is currently over, so users see inline description text without
    // waiting for the popup tooltip window's hover delay.
    juce::Label footerVersionLabel_;
    juce::Label footerTooltipLabel_;
    juce::Label footerCpuLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskAmpEditor)
};

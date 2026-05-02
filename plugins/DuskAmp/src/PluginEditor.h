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
    juce::TextButton tunerButton_;
    std::unique_ptr<TunerOverlay> tunerOverlay_;
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

    // Stored group box bounds (set in resized, drawn in paint)
    juce::Rectangle<int> inputGroupBounds_, outputGroupBounds_;
    juce::Rectangle<int> centerTopBounds_, centerMidBounds_, centerBotBounds_;
    juce::Rectangle<int> cabGroupBounds_, fxGroupBounds_;
    bool layoutIsNamMode_ = false;

    // Tooltip
    juce::TooltipWindow tooltipWindow_ { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskAmpEditor)
};

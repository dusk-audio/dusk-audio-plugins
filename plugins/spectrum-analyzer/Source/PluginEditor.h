#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "SpectrumAnalyzerLookAndFeel.h"
#include "UI/SpectrumDisplay.h"
#include "UI/MeterPanel.h"
#include "UI/HeaderBar.h"
#include "UI/SettingsOverlay.h"
#include "../../shared/SupportersOverlay.h"
#include "../../shared/LEDMeter.h"
#include "../../shared/ScalableEditorHelper.h"

//==============================================================================
class SpectrumAnalyzerEditor : public juce::AudioProcessorEditor,
                                public juce::Timer
{
public:
    explicit SpectrumAnalyzerEditor(SpectrumAnalyzerProcessor&);
    ~SpectrumAnalyzerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    SpectrumAnalyzerProcessor& audioProcessor;
    SpectrumAnalyzerLookAndFeel lookAndFeel;

    //==========================================================================
    // UI Components
    SpectrumDisplay spectrumDisplay;
    MeterPanel meterPanel;
    HeaderBar headerBar;

    // LED meters on right side
    LEDMeter outputMeterL{LEDMeter::Vertical};
    LEDMeter outputMeterR{LEDMeter::Vertical};

    // Settings overlay (always exists, shown/hidden on gear click)
    std::unique_ptr<SettingsOverlay> settingsOverlay;

    //==========================================================================
    // Supporters overlay
    std::unique_ptr<SupportersOverlay> supportersOverlay;

    //==========================================================================
    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftResolutionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slopeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> peakHoldAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rangeAttachment;

    //==========================================================================
    void setupAttachments();
    void updateMeters();

    void showSupportersPanel();
    void hideSupportersPanel();
    void showSettings();
    void hideSettings();

    ScalableEditorHelper resizeHelper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerEditor)
};

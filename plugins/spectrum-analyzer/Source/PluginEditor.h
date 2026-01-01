#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "SpectrumAnalyzerLookAndFeel.h"
#include "UI/SpectrumDisplay.h"
#include "UI/MeterPanel.h"
#include "UI/ToolbarComponent.h"
#include "../../shared/SupportersOverlay.h"
#include "../../shared/LEDMeter.h"

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
    void mouseDown(const juce::MouseEvent&) override;

private:
    SpectrumAnalyzerProcessor& audioProcessor;
    SpectrumAnalyzerLookAndFeel lookAndFeel;

    //==========================================================================
    // UI Components
    SpectrumDisplay spectrumDisplay;
    MeterPanel meterPanel;
    ToolbarComponent toolbar;

    // LED meters on right side
    LEDMeter outputMeterL{LEDMeter::Vertical};
    LEDMeter outputMeterR{LEDMeter::Vertical};

    // Header components
    juce::ComboBox channelModeCombo;
    juce::Label channelModeLabel;

    //==========================================================================
    // Supporters overlay
    std::unique_ptr<SupportersOverlay> supportersOverlay;
    juce::Rectangle<int> titleClickArea;

    //==========================================================================
    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftResolutionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slopeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> peakHoldAttachment;

    //==========================================================================
    void setupHeader();
    void setupAttachments();
    void updateMeters();

    void showSupportersPanel();
    void hideSupportersPanel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerEditor)
};

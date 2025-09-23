#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class StudioReverbLookAndFeel;

//==============================================================================
class StudioReverbAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    StudioReverbAudioProcessorEditor (StudioReverbAudioProcessor&);
    ~StudioReverbAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StudioReverbAudioProcessor& audioProcessor;
    std::unique_ptr<StudioReverbLookAndFeel> lookAndFeel;

    // UI Components
    juce::ComboBox reverbTypeCombo;
    juce::Label reverbTypeLabel;

    juce::Slider roomSizeSlider;
    juce::Label roomSizeLabel;

    juce::Slider dampingSlider;
    juce::Label dampingLabel;

    juce::Slider preDelaySlider;
    juce::Label preDelayLabel;

    juce::Slider decayTimeSlider;
    juce::Label decayTimeLabel;

    juce::Slider diffusionSlider;
    juce::Label diffusionLabel;

    juce::Slider wetLevelSlider;
    juce::Label wetLevelLabel;

    juce::Slider dryLevelSlider;
    juce::Label dryLevelLabel;

    juce::Slider widthSlider;
    juce::Label widthLabel;

    // APVTS attachments for thread-safe parameter binding
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> reverbTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> roomSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayTimeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;

    void setupSlider(juce::Slider& slider, juce::Label& label,
                    const juce::String& labelText,
                    int decimalPlaces = 1);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioReverbAudioProcessorEditor)
};
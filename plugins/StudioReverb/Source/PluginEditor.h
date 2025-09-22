#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class StudioReverbAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    StudioReverbAudioProcessorEditor (StudioReverbAudioProcessor&);
    ~StudioReverbAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    StudioReverbAudioProcessor& audioProcessor;

    // UI Components
    juce::Slider roomSizeSlider;
    juce::Label roomSizeLabel;

    juce::Slider dampingSlider;
    juce::Label dampingLabel;

    juce::Slider wetLevelSlider;
    juce::Label wetLevelLabel;

    juce::Slider dryLevelSlider;
    juce::Label dryLevelLabel;

    juce::Slider widthSlider;
    juce::Label widthLabel;

    void setupSlider(juce::Slider& slider, juce::Label& label,
                    const juce::String& labelText, const juce::String& suffix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioReverbAudioProcessorEditor)
};
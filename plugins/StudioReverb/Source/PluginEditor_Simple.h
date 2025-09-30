#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class StudioReverbAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    StudioReverbAudioProcessorEditor(StudioReverbAudioProcessor&);
    ~StudioReverbAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    StudioReverbAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioReverbAudioProcessorEditor)
};
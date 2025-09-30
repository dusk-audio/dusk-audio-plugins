#include "PluginProcessor.h"
#include "PluginEditor_Simple.h"

StudioReverbAudioProcessorEditor::StudioReverbAudioProcessorEditor(
    StudioReverbAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // Very simple UI - just set size
    setSize(400, 300);
}

StudioReverbAudioProcessorEditor::~StudioReverbAudioProcessorEditor()
{
}

void StudioReverbAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Simple dark background
    g.fillAll(juce::Colours::black);

    // Simple text
    g.setColour(juce::Colours::white);
    g.setFont(15.0f);
    g.drawFittedText("Studio Reverb", getLocalBounds(), juce::Justification::centred, 1);
}

void StudioReverbAudioProcessorEditor::resized()
{
    // Nothing to resize
}
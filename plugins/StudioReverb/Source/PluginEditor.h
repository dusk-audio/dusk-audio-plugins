#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class StudioReverbLookAndFeel;

//==============================================================================
class StudioReverbAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                          public juce::ComboBox::Listener
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
    juce::ComboBox presetCombo;
    juce::Label presetLabel;

    // Mix Controls (4 sliders like Dragonfly)
    juce::Slider dryLevelSlider;
    juce::Label dryLevelLabel;
    juce::Slider earlyLevelSlider;
    juce::Label earlyLevelLabel;
    juce::Slider earlySendSlider;
    juce::Label earlySendLabel;
    juce::Slider lateLevelSlider;
    juce::Label lateLevelLabel;

    // Basic Controls
    juce::Slider sizeSlider;
    juce::Label sizeLabel;
    juce::Slider widthSlider;
    juce::Label widthLabel;
    juce::Slider preDelaySlider;
    juce::Label preDelayLabel;
    juce::Slider decaySlider;
    juce::Label decayLabel;
    juce::Slider diffuseSlider;
    juce::Label diffuseLabel;

    // Modulation Controls
    juce::Slider spinSlider;
    juce::Label spinLabel;
    juce::Slider wanderSlider;
    juce::Label wanderLabel;

    // Filter Controls
    juce::Slider highCutSlider;
    juce::Label highCutLabel;
    juce::Slider lowCutSlider;
    juce::Label lowCutLabel;

    // Hall-specific Crossover Controls
    juce::Slider lowCrossSlider;
    juce::Label lowCrossLabel;
    juce::Slider highCrossSlider;
    juce::Label highCrossLabel;
    juce::Slider lowMultSlider;
    juce::Label lowMultLabel;
    juce::Slider highMultSlider;
    juce::Label highMultLabel;

    // APVTS attachments for thread-safe parameter binding
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> reverbTypeAttachment;

    // Mix Control Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> earlyLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> earlySendAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lateLevelAttachment;

    // Basic Control Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffuseAttachment;

    // Modulation Control Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spinAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wanderAttachment;

    // Filter Control Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;

    // Hall-specific Crossover Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCrossAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCrossAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowMultAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highMultAttachment;

    void setupSlider(juce::Slider& slider, juce::Label& label,
                    const juce::String& labelText,
                    int decimalPlaces = 1);

    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void updateHallControlsVisibility();
    void updatePresetList();
    void updatePresetListForAlgorithm(int algorithmIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioReverbAudioProcessorEditor)
};
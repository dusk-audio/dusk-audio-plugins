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
    int currentReverbIndex = 0;  // Track current reverb type in editor

    // UI Components
    juce::ComboBox reverbTypeCombo;
    juce::Label reverbTypeLabel;
    juce::ComboBox presetCombo;
    juce::Label presetLabel;

    // Mix Controls - separate dry and wet levels
    juce::Slider dryLevelSlider;
    juce::Label dryLevelLabel;
    juce::Slider wetLevelSlider;
    juce::Label wetLevelLabel;
    juce::Slider earlyLevelSlider;
    juce::Label earlyLevelLabel;
    juce::Slider earlySendSlider;
    juce::Label earlySendLabel;

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
    juce::Slider modulationSlider;  // Hall-specific
    juce::Label modulationLabel;

    // Filter Controls
    juce::Slider highCutSlider;
    juce::Label highCutLabel;
    juce::Slider lowCutSlider;
    juce::Label lowCutLabel;
    juce::Slider dampenSlider;      // Plate-specific
    juce::Label dampenLabel;
    juce::Slider earlyDampSlider;   // Room-specific
    juce::Label earlyDampLabel;
    juce::Slider lateDampSlider;    // Room-specific
    juce::Label lateDampLabel;

    // Room-specific Boost Controls
    juce::Slider lowBoostSlider;
    juce::Label lowBoostLabel;
    juce::Slider boostFreqSlider;
    juce::Label boostFreqLabel;

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
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> earlyLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> earlySendAttachment;

    // Basic Control Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffuseAttachment;

    // Modulation Control Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spinAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wanderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modulationAttachment;

    // Filter Control Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampenAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> earlyDampAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lateDampAttachment;

    // Room Boost Control Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowBoostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> boostFreqAttachment;

    // Hall-specific Crossover Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCrossAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCrossAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowMultAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highMultAttachment;

    void setupSlider(juce::Slider& slider, juce::Label& label,
                    const juce::String& labelText,
                    int decimalPlaces = 1);

    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void updateHallControlsVisibility(int reverbIndex);
    void updatePresetList();
    void updatePresetListForAlgorithm(int algorithmIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioReverbAudioProcessorEditor)
};
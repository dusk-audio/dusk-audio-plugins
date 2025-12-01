#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrummerDNA.h"

class DrummerCloneAudioProcessor;

/**
 * ProfileEditorPanel - Custom drummer profile editor
 *
 * Allows users to create and modify drummer personality profiles
 * with real-time preview of the changes.
 */
class ProfileEditorPanel : public juce::Component,
                           public juce::Slider::Listener,
                           public juce::Button::Listener
{
public:
    ProfileEditorPanel(DrummerCloneAudioProcessor& processor);
    ~ProfileEditorPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Listeners
    void sliderValueChanged(juce::Slider* slider) override;
    void buttonClicked(juce::Button* button) override;

    // Set the profile to edit
    void loadProfile(const DrummerProfile& profile);
    DrummerProfile getCurrentProfile() const;

    // Callback when profile changes
    std::function<void(const DrummerProfile&)> onProfileChanged;

private:
    DrummerCloneAudioProcessor& audioProcessor;

    // Current profile being edited
    DrummerProfile currentProfile;

    // Name and style
    juce::Label nameLabel;
    juce::TextEditor nameEditor;
    juce::Label styleLabel;
    juce::ComboBox styleComboBox;

    // Personality sliders
    juce::Label aggressionLabel;
    juce::Slider aggressionSlider;

    juce::Label grooveBiasLabel;
    juce::Slider grooveBiasSlider;

    juce::Label ghostNotesLabel;
    juce::Slider ghostNotesSlider;

    juce::Label fillHungerLabel;
    juce::Slider fillHungerSlider;

    juce::Label tomLoveLabel;
    juce::Slider tomLoveSlider;

    juce::Label ridePreferenceLabel;
    juce::Slider ridePreferenceSlider;

    juce::Label crashHappinessLabel;
    juce::Slider crashHappinessSlider;

    juce::Label simplicityLabel;
    juce::Slider simplicitySlider;

    juce::Label laidBackLabel;
    juce::Slider laidBackSlider;

    // Technical settings
    juce::Label divisionLabel;
    juce::ComboBox divisionComboBox;

    juce::Label swingLabel;
    juce::Slider swingSlider;

    juce::Label velocityFloorLabel;
    juce::Slider velocityFloorSlider;

    juce::Label velocityCeilingLabel;
    juce::Slider velocityCeilingSlider;

    // Buttons
    juce::TextButton saveButton;
    juce::TextButton loadButton;
    juce::TextButton resetButton;

    // Bio editor
    juce::Label bioLabel;
    juce::TextEditor bioEditor;

    // Helper methods
    void setupSlider(juce::Slider& slider, juce::Label& label,
                     const juce::String& labelText, float min, float max, float defaultVal);
    void updateProfileFromUI();
    void updateUIFromProfile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProfileEditorPanel)
};

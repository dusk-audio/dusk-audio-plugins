#include "ProfileEditorPanel.h"
#include "PluginProcessor.h"

ProfileEditorPanel::ProfileEditorPanel(DrummerCloneAudioProcessor& processor)
    : audioProcessor(processor)
{
    // Name field
    nameLabel.setText("Name:", juce::dontSendNotification);
    nameLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(nameLabel);

    nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(50, 50, 55));
    nameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    nameEditor.onTextChange = [this]() { updateProfileFromUI(); };
    addAndMakeVisible(nameEditor);

    // Style selection
    styleLabel.setText("Style:", juce::dontSendNotification);
    styleLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(styleLabel);

    styleComboBox.addItem("Rock", 1);
    styleComboBox.addItem("HipHop", 2);
    styleComboBox.addItem("Alternative", 3);
    styleComboBox.addItem("R&B", 4);
    styleComboBox.addItem("Electronic", 5);
    styleComboBox.addItem("Trap", 6);
    styleComboBox.addItem("Songwriter", 7);
    styleComboBox.setSelectedId(1);
    styleComboBox.onChange = [this]() { updateProfileFromUI(); };
    addAndMakeVisible(styleComboBox);

    // Personality sliders
    setupSlider(aggressionSlider, aggressionLabel, "Aggression", 0.0f, 1.0f, 0.5f);
    setupSlider(grooveBiasSlider, grooveBiasLabel, "Swing Bias", 0.0f, 1.0f, 0.5f);
    setupSlider(ghostNotesSlider, ghostNotesLabel, "Ghost Notes", 0.0f, 1.0f, 0.3f);
    setupSlider(fillHungerSlider, fillHungerLabel, "Fill Hunger", 0.0f, 1.0f, 0.3f);
    setupSlider(tomLoveSlider, tomLoveLabel, "Tom Love", 0.0f, 1.0f, 0.3f);
    setupSlider(ridePreferenceSlider, ridePreferenceLabel, "Ride Pref", 0.0f, 1.0f, 0.3f);
    setupSlider(crashHappinessSlider, crashHappinessLabel, "Crash Happy", 0.0f, 1.0f, 0.4f);
    setupSlider(simplicitySlider, simplicityLabel, "Simplicity", 0.0f, 1.0f, 0.5f);
    setupSlider(laidBackSlider, laidBackLabel, "Laid Back", -1.0f, 1.0f, 0.0f);

    // Technical settings
    divisionLabel.setText("Division:", juce::dontSendNotification);
    divisionLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(divisionLabel);

    divisionComboBox.addItem("8th Notes", 1);
    divisionComboBox.addItem("16th Notes", 2);
    divisionComboBox.setSelectedId(2);
    divisionComboBox.onChange = [this]() { updateProfileFromUI(); };
    addAndMakeVisible(divisionComboBox);

    setupSlider(swingSlider, swingLabel, "Swing", 0.0f, 0.5f, 0.0f);
    setupSlider(velocityFloorSlider, velocityFloorLabel, "Vel Floor", 1, 100, 40);
    setupSlider(velocityCeilingSlider, velocityCeilingLabel, "Vel Ceiling", 50, 127, 127);

    // Bio field
    bioLabel.setText("Bio:", juce::dontSendNotification);
    bioLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(bioLabel);

    bioEditor.setMultiLine(true);
    bioEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(50, 50, 55));
    bioEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    bioEditor.onTextChange = [this]() { updateProfileFromUI(); };
    addAndMakeVisible(bioEditor);

    // Buttons
    saveButton.setButtonText("Save");
    saveButton.addListener(this);
    addAndMakeVisible(saveButton);

    loadButton.setButtonText("Load");
    loadButton.addListener(this);
    addAndMakeVisible(loadButton);

    resetButton.setButtonText("Reset");
    resetButton.addListener(this);
    addAndMakeVisible(resetButton);

    // Load a default profile
    DrummerProfile defaultProfile;
    defaultProfile.name = "Custom";
    defaultProfile.style = "Rock";
    defaultProfile.bio = "A custom drummer profile";
    loadProfile(defaultProfile);
}

ProfileEditorPanel::~ProfileEditorPanel()
{
}

void ProfileEditorPanel::setupSlider(juce::Slider& slider, juce::Label& label,
                                      const juce::String& labelText, float min, float max, float defaultVal)
{
    label.setText(labelText, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(juce::FontOptions(10.0f));
    addAndMakeVisible(label);

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 16);
    slider.setRange(static_cast<double>(min), static_cast<double>(max), 0.01);
    slider.setValue(static_cast<double>(defaultVal));
    slider.addListener(this);
    addAndMakeVisible(slider);
}

void ProfileEditorPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Panel background
    g.setColour(juce::Colour(35, 35, 40));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Border
    g.setColour(juce::Colour(60, 60, 70));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
    g.drawText("PROFILE EDITOR", bounds.removeFromTop(25).reduced(10, 5), juce::Justification::left);
}

void ProfileEditorPanel::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    bounds.removeFromTop(25);  // Title area

    int rowHeight = 22;
    int labelWidth = 70;
    int spacing = 3;

    // Name row
    auto nameRow = bounds.removeFromTop(rowHeight);
    nameLabel.setBounds(nameRow.removeFromLeft(labelWidth));
    nameEditor.setBounds(nameRow.reduced(2));
    bounds.removeFromTop(spacing);

    // Style row
    auto styleRow = bounds.removeFromTop(rowHeight);
    styleLabel.setBounds(styleRow.removeFromLeft(labelWidth));
    styleComboBox.setBounds(styleRow.reduced(2));
    bounds.removeFromTop(spacing);

    // Two-column layout for sliders
    auto leftCol = bounds.removeFromLeft(bounds.getWidth() / 2).reduced(2);
    auto rightCol = bounds.reduced(2);

    // Left column - Personality traits
    auto aggrRow = leftCol.removeFromTop(rowHeight);
    aggressionLabel.setBounds(aggrRow.removeFromLeft(labelWidth));
    aggressionSlider.setBounds(aggrRow);
    leftCol.removeFromTop(spacing);

    auto grooveRow = leftCol.removeFromTop(rowHeight);
    grooveBiasLabel.setBounds(grooveRow.removeFromLeft(labelWidth));
    grooveBiasSlider.setBounds(grooveRow);
    leftCol.removeFromTop(spacing);

    auto ghostRow = leftCol.removeFromTop(rowHeight);
    ghostNotesLabel.setBounds(ghostRow.removeFromLeft(labelWidth));
    ghostNotesSlider.setBounds(ghostRow);
    leftCol.removeFromTop(spacing);

    auto fillRow = leftCol.removeFromTop(rowHeight);
    fillHungerLabel.setBounds(fillRow.removeFromLeft(labelWidth));
    fillHungerSlider.setBounds(fillRow);
    leftCol.removeFromTop(spacing);

    auto tomRow = leftCol.removeFromTop(rowHeight);
    tomLoveLabel.setBounds(tomRow.removeFromLeft(labelWidth));
    tomLoveSlider.setBounds(tomRow);
    leftCol.removeFromTop(spacing);

    // Right column - More personality and technical
    auto rideRow = rightCol.removeFromTop(rowHeight);
    ridePreferenceLabel.setBounds(rideRow.removeFromLeft(labelWidth));
    ridePreferenceSlider.setBounds(rideRow);
    rightCol.removeFromTop(spacing);

    auto crashRow = rightCol.removeFromTop(rowHeight);
    crashHappinessLabel.setBounds(crashRow.removeFromLeft(labelWidth));
    crashHappinessSlider.setBounds(crashRow);
    rightCol.removeFromTop(spacing);

    auto simpRow = rightCol.removeFromTop(rowHeight);
    simplicityLabel.setBounds(simpRow.removeFromLeft(labelWidth));
    simplicitySlider.setBounds(simpRow);
    rightCol.removeFromTop(spacing);

    auto laidRow = rightCol.removeFromTop(rowHeight);
    laidBackLabel.setBounds(laidRow.removeFromLeft(labelWidth));
    laidBackSlider.setBounds(laidRow);
    rightCol.removeFromTop(spacing);

    auto divRow = rightCol.removeFromTop(rowHeight);
    divisionLabel.setBounds(divRow.removeFromLeft(labelWidth));
    divisionComboBox.setBounds(divRow);
    rightCol.removeFromTop(spacing);

    // Remaining items at the bottom of left column
    auto swingRow = leftCol.removeFromTop(rowHeight);
    swingLabel.setBounds(swingRow.removeFromLeft(labelWidth));
    swingSlider.setBounds(swingRow);
    leftCol.removeFromTop(spacing);

    auto velFloorRow = leftCol.removeFromTop(rowHeight);
    velocityFloorLabel.setBounds(velFloorRow.removeFromLeft(labelWidth));
    velocityFloorSlider.setBounds(velFloorRow);
    leftCol.removeFromTop(spacing);

    auto velCeilRow = leftCol.removeFromTop(rowHeight);
    velocityCeilingLabel.setBounds(velCeilRow.removeFromLeft(labelWidth));
    velocityCeilingSlider.setBounds(velCeilRow);
    leftCol.removeFromTop(spacing);

    // Bio at the bottom of right column
    bioLabel.setBounds(rightCol.removeFromTop(18));
    bioEditor.setBounds(rightCol.removeFromTop(50));
    rightCol.removeFromTop(spacing);

    // Buttons at the bottom
    auto buttonRow = rightCol.removeFromTop(28);
    int buttonWidth = buttonRow.getWidth() / 3 - 4;
    saveButton.setBounds(buttonRow.removeFromLeft(buttonWidth).reduced(2));
    loadButton.setBounds(buttonRow.removeFromLeft(buttonWidth).reduced(2));
    resetButton.setBounds(buttonRow.reduced(2));
}

void ProfileEditorPanel::sliderValueChanged(juce::Slider* slider)
{
    // Enforce velocity floor <= ceiling constraint in real-time
    if (slider == &velocityFloorSlider)
    {
        if (velocityFloorSlider.getValue() > velocityCeilingSlider.getValue())
            velocityCeilingSlider.setValue(velocityFloorSlider.getValue(), juce::dontSendNotification);
    }
    else if (slider == &velocityCeilingSlider)
    {
        if (velocityCeilingSlider.getValue() < velocityFloorSlider.getValue())
            velocityFloorSlider.setValue(velocityCeilingSlider.getValue(), juce::dontSendNotification);
    }

    updateProfileFromUI();
}

void ProfileEditorPanel::buttonClicked(juce::Button* button)
{
    if (button == &saveButton)
    {
        // Sanitize the profile name for use as a filename
        juce::String safeName = juce::File::createLegalFileName(currentProfile.name).trim();
        if (safeName.isEmpty())
            safeName = "profile";

        // Save profile to JSON file
        auto fileChooser = std::make_shared<juce::FileChooser>(
            "Save Drummer Profile",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(safeName + ".json"),
            "*.json");

        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this, fileChooser](const juce::FileChooser& chooser)
            {
                auto file = chooser.getResult();
                if (file != juce::File())
                {
                    if (!file.hasFileExtension(".json"))
                        file = file.withFileExtension(".json");

                    DrummerDNA::saveToJSON(currentProfile, file);
                }
            });
    }
    else if (button == &loadButton)
    {
        // Load profile from JSON file
        auto fileChooser = std::make_shared<juce::FileChooser>(
            "Load Drummer Profile",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.json");

        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, fileChooser](const juce::FileChooser& chooser)
            {
                auto file = chooser.getResult();
                if (file != juce::File() && file.existsAsFile())
                {
                    auto profile = DrummerDNA::loadFromJSON(file);
                    if (!profile.name.isEmpty())
                    {
                        loadProfile(profile);
                    }
                }
            });
    }
    else if (button == &resetButton)
    {
        // Reset to defaults
        DrummerProfile defaultProfile;
        defaultProfile.name = "Custom";
        defaultProfile.style = "Rock";
        defaultProfile.bio = "A custom drummer profile";
        loadProfile(defaultProfile);
    }
}

void ProfileEditorPanel::loadProfile(const DrummerProfile& profile)
{
    currentProfile = profile;
    updateUIFromProfile();

    if (onProfileChanged)
        onProfileChanged(currentProfile);
}

DrummerProfile ProfileEditorPanel::getCurrentProfile() const
{
    return currentProfile;
}

void ProfileEditorPanel::updateProfileFromUI()
{
    currentProfile.name = nameEditor.getText();

    // Map combo box to style name
    static const juce::StringArray styles = {"Rock", "HipHop", "Alternative", "R&B", "Electronic", "Trap", "Songwriter"};
    int styleIdx = styleComboBox.getSelectedId() - 1;
    if (styleIdx >= 0 && styleIdx < styles.size())
        currentProfile.style = styles[styleIdx];

    currentProfile.aggression = static_cast<float>(aggressionSlider.getValue());
    currentProfile.grooveBias = static_cast<float>(grooveBiasSlider.getValue());
    currentProfile.ghostNotes = static_cast<float>(ghostNotesSlider.getValue());
    currentProfile.fillHunger = static_cast<float>(fillHungerSlider.getValue());
    currentProfile.tomLove = static_cast<float>(tomLoveSlider.getValue());
    currentProfile.ridePreference = static_cast<float>(ridePreferenceSlider.getValue());
    currentProfile.crashHappiness = static_cast<float>(crashHappinessSlider.getValue());
    currentProfile.simplicity = static_cast<float>(simplicitySlider.getValue());
    currentProfile.laidBack = static_cast<float>(laidBackSlider.getValue());

    currentProfile.preferredDivision = (divisionComboBox.getSelectedId() == 1) ? 8 : 16;
    currentProfile.swingDefault = static_cast<float>(swingSlider.getValue());

    // Read velocity floor/ceiling and ensure floor <= ceiling
    int floor = static_cast<int>(velocityFloorSlider.getValue());
    int ceiling = static_cast<int>(velocityCeilingSlider.getValue());
    if (floor > ceiling)
    {
        // Clamp floor to ceiling value and update the UI
        floor = ceiling;
        velocityFloorSlider.setValue(floor, juce::dontSendNotification);
    }
    currentProfile.velocityFloor = floor;
    currentProfile.velocityCeiling = ceiling;

    currentProfile.bio = bioEditor.getText();

    if (onProfileChanged)
        onProfileChanged(currentProfile);
}

void ProfileEditorPanel::updateUIFromProfile()
{
    nameEditor.setText(currentProfile.name, juce::dontSendNotification);

    // Map style name to combo box
    static const juce::StringArray styles = {"Rock", "HipHop", "Alternative", "R&B", "Electronic", "Trap", "Songwriter"};
    int styleIdx = styles.indexOf(currentProfile.style);
    if (styleIdx >= 0)
        styleComboBox.setSelectedId(styleIdx + 1, juce::dontSendNotification);

    aggressionSlider.setValue(currentProfile.aggression, juce::dontSendNotification);
    grooveBiasSlider.setValue(currentProfile.grooveBias, juce::dontSendNotification);
    ghostNotesSlider.setValue(currentProfile.ghostNotes, juce::dontSendNotification);
    fillHungerSlider.setValue(currentProfile.fillHunger, juce::dontSendNotification);
    tomLoveSlider.setValue(currentProfile.tomLove, juce::dontSendNotification);
    ridePreferenceSlider.setValue(currentProfile.ridePreference, juce::dontSendNotification);
    crashHappinessSlider.setValue(currentProfile.crashHappiness, juce::dontSendNotification);
    simplicitySlider.setValue(currentProfile.simplicity, juce::dontSendNotification);
    laidBackSlider.setValue(currentProfile.laidBack, juce::dontSendNotification);

    divisionComboBox.setSelectedId((currentProfile.preferredDivision == 8) ? 1 : 2, juce::dontSendNotification);
    swingSlider.setValue(currentProfile.swingDefault, juce::dontSendNotification);

    // Ensure floor <= ceiling when loading profile (fix potentially invalid saved data)
    int floor = currentProfile.velocityFloor;
    int ceiling = currentProfile.velocityCeiling;
    if (floor > ceiling)
        floor = ceiling;
    velocityFloorSlider.setValue(floor, juce::dontSendNotification);
    velocityCeilingSlider.setValue(ceiling, juce::dontSendNotification);

    bioEditor.setText(currentProfile.bio, juce::dontSendNotification);
}

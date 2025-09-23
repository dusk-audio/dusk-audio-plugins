#include "PluginProcessor.h"
#include "PluginEditor.h"

// Custom Look and Feel for cohesive theme
class StudioReverbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioReverbLookAndFeel()
    {
        // Match the color scheme from other plugins
        backgroundColour = juce::Colour(0xff1a1a1a);
        knobColour = juce::Colour(0xff3a3a3a);
        pointerColour = juce::Colour(0xffff6b35);  // Orange accent
        accentColour = juce::Colour(0xff8b4513);   // Darker orange

        setColour(juce::Slider::thumbColourId, pointerColour);
        setColour(juce::Slider::rotarySliderFillColourId, accentColour);
        setColour(juce::Slider::rotarySliderOutlineColourId, knobColour);
        setColour(juce::ComboBox::backgroundColourId, knobColour);
        setColour(juce::ComboBox::textColourId, juce::Colours::lightgrey);
        setColour(juce::PopupMenu::backgroundColourId, backgroundColour);
        setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
        auto centreX = x + width * 0.5f;
        auto centreY = y + height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Shadow
        g.setColour(juce::Colour(0x60000000));
        g.fillEllipse(rx + 2, ry + 2, rw, rw);

        // Outer metallic ring
        juce::ColourGradient outerGradient(
            juce::Colour(0xff5a5a5a), centreX - radius, centreY,
            juce::Colour(0xff2a2a2a), centreX + radius, centreY, false);
        g.setGradientFill(outerGradient);
        g.fillEllipse(rx - 3, ry - 3, rw + 6, rw + 6);

        // Inner knob body
        juce::ColourGradient bodyGradient(
            juce::Colour(0xff4a4a4a), centreX - radius * 0.7f, centreY - radius * 0.7f,
            juce::Colour(0xff1a1a1a), centreX + radius * 0.7f, centreY + radius * 0.7f, true);
        g.setGradientFill(bodyGradient);
        g.fillEllipse(rx, ry, rw, rw);

        // Inner ring detail
        g.setColour(juce::Colour(0xff2a2a2a));
        g.drawEllipse(rx + 4, ry + 4, rw - 8, rw - 8, 2.0f);

        // Center cap
        auto capRadius = radius * 0.3f;
        juce::ColourGradient capGradient(
            juce::Colour(0xff6a6a6a), centreX - capRadius, centreY - capRadius,
            juce::Colour(0xff3a3a3a), centreX + capRadius, centreY + capRadius, false);
        g.setGradientFill(capGradient);
        g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

        // Position indicator with glow
        juce::Path pointer;
        pointer.addRectangle(-2.0f, -radius + 6, 4.0f, radius * 0.4f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        g.setColour(pointerColour.withAlpha(0.3f));
        g.strokePath(pointer, juce::PathStrokeType(6.0f));
        g.setColour(pointerColour);
        g.fillPath(pointer);

        // Tick marks
        for (int i = 0; i <= 10; ++i)
        {
            auto tickAngle = rotaryStartAngle + (i / 10.0f) * (rotaryEndAngle - rotaryStartAngle);
            auto tickLength = (i == 0 || i == 5 || i == 10) ? radius * 0.15f : radius * 0.1f;

            juce::Path tick;
            tick.addRectangle(-1.0f, -radius - 8, 2.0f, tickLength);
            tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(centreX, centreY));

            g.setColour(juce::Colour(0xffaaaaaa).withAlpha(0.7f));
            g.fillPath(tick);
        }

        // Center screw detail
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillEllipse(centreX - 3, centreY - 3, 6, 6);
        g.setColour(juce::Colour(0xff4a4a4a));
        g.drawEllipse(centreX - 3, centreY - 3, 6, 6, 0.5f);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                     int, int, int, int, juce::ComboBox& box) override
    {
        auto cornerSize = box.findParentComponentOfClass<juce::ChoicePropertyComponent>() != nullptr ? 0.0f : 3.0f;
        juce::Rectangle<int> boxBounds (0, 0, width, height);

        g.setColour(knobColour);
        g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);

        g.setColour(juce::Colour(0xff5a5a5a));
        g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f, 0.5f), cornerSize, 1.0f);

        juce::Rectangle<int> arrowZone (width - 30, 0, 20, height);
        juce::Path path;
        path.startNewSubPath(arrowZone.getX() + 3.0f, arrowZone.getCentreY() - 2.0f);
        path.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
        path.lineTo(arrowZone.getRight() - 3.0f, arrowZone.getCentreY() - 2.0f);

        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.2f));
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

private:
    juce::Colour backgroundColour, knobColour, pointerColour, accentColour;
};

//==============================================================================
StudioReverbAudioProcessorEditor::StudioReverbAudioProcessorEditor (StudioReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Set custom look and feel
    lookAndFeel = std::make_unique<StudioReverbLookAndFeel>();
    setLookAndFeel(lookAndFeel.get());

    // Make plugin resizable with reasonable limits
    setResizable(true, true);
    setResizeLimits(600, 600, 1000, 750);
    setSize (800, 650);

    // Reverb Type Selector with improved styling
    addAndMakeVisible(reverbTypeCombo);
    reverbTypeCombo.addItemList({"Room", "Hall", "Plate", "Early Reflections"}, 1);
    reverbTypeCombo.setJustificationType(juce::Justification::centred);
    reverbTypeCombo.addListener(this);
    reverbTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "reverbType", reverbTypeCombo);

    addAndMakeVisible(reverbTypeLabel);
    reverbTypeLabel.setText("Reverb Type", juce::dontSendNotification);
    reverbTypeLabel.setJustificationType(juce::Justification::centred);
    reverbTypeLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    reverbTypeLabel.attachToComponent(&reverbTypeCombo, false);

    // Preset Selector
    addAndMakeVisible(presetCombo);
    presetCombo.setJustificationType(juce::Justification::centred);
    presetCombo.addListener(this);

    DBG("=== StudioReverbAudioProcessorEditor Constructor ===");
    DBG("Initial reverb type index: " << (audioProcessor.reverbType ? audioProcessor.reverbType->getIndex() : -1));
    DBG("Initial reverb type name: " << (audioProcessor.reverbType ? audioProcessor.reverbType->getCurrentChoiceName() : "null"));

    updatePresetList();

    addAndMakeVisible(presetLabel);
    presetLabel.setText("Preset", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centred);
    presetLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    presetLabel.attachToComponent(&presetCombo, false);

    // === Mix Controls (4 sliders like Dragonfly) ===
    setupSlider(dryLevelSlider, dryLevelLabel, "Dry", 1);
    dryLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "dryLevel", dryLevelSlider);

    setupSlider(earlyLevelSlider, earlyLevelLabel, "Early", 1);
    earlyLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "earlyLevel", earlyLevelSlider);

    setupSlider(earlySendSlider, earlySendLabel, "Early Send", 1);
    earlySendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "earlySend", earlySendSlider);

    setupSlider(lateLevelSlider, lateLevelLabel, "Late", 1);
    lateLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "lateLevel", lateLevelSlider);

    // === Basic Controls ===
    setupSlider(sizeSlider, sizeLabel, "Size", 1);
    sizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "size", sizeSlider);

    setupSlider(widthSlider, widthLabel, "Width", 1);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "width", widthSlider);

    setupSlider(preDelaySlider, preDelayLabel, "Pre-Delay", 1);
    preDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "preDelay", preDelaySlider);

    setupSlider(decaySlider, decayLabel, "Decay", 2);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "decay", decaySlider);

    setupSlider(diffuseSlider, diffuseLabel, "Diffuse", 1);
    diffuseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "diffuse", diffuseSlider);

    // === Modulation Controls ===
    setupSlider(spinSlider, spinLabel, "Spin", 2);
    spinAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "spin", spinSlider);

    setupSlider(wanderSlider, wanderLabel, "Wander", 2);
    wanderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "wander", wanderSlider);

    // === Filter Controls ===
    setupSlider(highCutSlider, highCutLabel, "High Cut", 0);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "highCut", highCutSlider);

    setupSlider(lowCutSlider, lowCutLabel, "Low Cut", 0);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "lowCut", lowCutSlider);

    // === Hall-specific Crossover Controls ===
    setupSlider(lowCrossSlider, lowCrossLabel, "Low Cross", 0);
    lowCrossAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "lowCross", lowCrossSlider);

    setupSlider(highCrossSlider, highCrossLabel, "High Cross", 0);
    highCrossAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "highCross", highCrossSlider);

    setupSlider(lowMultSlider, lowMultLabel, "Low Mult", 2);
    lowMultAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "lowMult", lowMultSlider);

    setupSlider(highMultSlider, highMultLabel, "High Mult", 2);
    highMultAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "highMult", highMultSlider);
}

StudioReverbAudioProcessorEditor::~StudioReverbAudioProcessorEditor()
{
    reverbTypeCombo.removeListener(this);
    presetCombo.removeListener(this);
    setLookAndFeel(nullptr);
}

void StudioReverbAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label,
                                                   const juce::String& labelText,
                                                   int decimalPlaces)
{
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    // Don't set suffix here - it's already set in the parameter's lambda in PluginProcessor
    // slider.setTextValueSuffix(" " + suffix);
    slider.setNumDecimalPlacesToDisplay(decimalPlaces);

    addAndMakeVisible(label);
    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(12.0f));
    label.attachToComponent(&slider, false);
}

//==============================================================================
void StudioReverbAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Dark background matching other plugins
    g.fillAll(juce::Colour(0xff1a1a1a));

    // Title area with gradient
    auto titleBounds = getLocalBounds().removeFromTop(50);
    juce::ColourGradient titleGradient(
        juce::Colour(0xff2a2a2a), 0, 0,
        juce::Colour(0xff1a1a1a), 0, titleBounds.getHeight(), false);
    g.setGradientFill(titleGradient);
    g.fillRect(titleBounds);

    // Title text
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(28.0f, juce::Font::bold));
    g.drawText("StudioReverb", titleBounds, juce::Justification::centred, true);

    // Company name
    g.setFont(juce::Font(12.0f));
    g.setColour(juce::Colours::grey);
    g.drawText("Luna Co. Audio", titleBounds.removeFromBottom(20),
               juce::Justification::centred, true);

    // Draw section backgrounds
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50);
    bounds.reduce(15, 15);

    // Type and Preset selector section
    auto selectorArea = bounds.removeFromTop(120);
    g.setColour(juce::Colour(0x20ffffff));
    g.fillRoundedRectangle(selectorArea.toFloat(), 8.0f);
    g.setColour(juce::Colour(0x40ffffff));
    g.drawRoundedRectangle(selectorArea.toFloat(), 8.0f, 1.0f);

    bounds.removeFromTop(15);

    const int sliderSize = 75;

    // Mix Controls Section
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.setFont(11.0f);
    g.drawText("MIX LEVELS", bounds.removeFromTop(15), juce::Justification::centredLeft, false);

    auto mixArea = bounds.removeFromTop(sliderSize + 25);
    g.setColour(juce::Colour(0x15ffffff));
    g.fillRoundedRectangle(mixArea.toFloat(), 6.0f);

    // Basic Controls Section
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.drawText("REVERB CHARACTER", bounds.removeFromTop(15), juce::Justification::centredLeft, false);

    auto basicArea = bounds.removeFromTop(sliderSize + 25);
    g.setColour(juce::Colour(0x15ffffff));
    g.fillRoundedRectangle(basicArea.toFloat(), 6.0f);

    // Modulation & Filter Section
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.drawText("MODULATION & FILTERS", bounds.removeFromTop(15), juce::Justification::centredLeft, false);

    auto modArea = bounds.removeFromTop(sliderSize + 25);
    g.setColour(juce::Colour(0x15ffffff));
    g.fillRoundedRectangle(modArea.toFloat(), 6.0f);

    // Hall-specific Crossover Section (if visible)
    if (audioProcessor.reverbType && audioProcessor.reverbType->getIndex() == 1)
    {
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("HALL CROSSOVER", bounds.removeFromTop(15), juce::Justification::centredLeft, false);

        auto crossArea = bounds.removeFromTop(sliderSize + 25);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(crossArea.toFloat(), 6.0f);
    }
}

void StudioReverbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50); // Title area
    bounds.reduce(20, 20);

    // Reverb Type and Preset Selectors
    auto selectorArea = bounds.removeFromTop(120);

    // Type selector
    auto typeArea = selectorArea.removeFromTop(60);
    typeArea.removeFromTop(25); // Label space
    reverbTypeCombo.setBounds(typeArea.reduced(120, 8));

    // Preset selector
    auto presetArea = selectorArea;
    presetArea.removeFromTop(25); // Label space
    presetCombo.setBounds(presetArea.reduced(120, 8));

    bounds.removeFromTop(10); // Spacing

    const int sliderSize = 75;
    const int labelHeight = 20;
    const int spacing = 10;

    // === Mix Controls Section ===
    auto mixSection = bounds.removeFromTop(sliderSize + labelHeight + 25);
    mixSection.removeFromTop(labelHeight);

    int mixStartX = (mixSection.getWidth() - (sliderSize * 4 + spacing * 3)) / 2;
    dryLevelSlider.setBounds(mixStartX, mixSection.getY(), sliderSize, sliderSize);
    earlyLevelSlider.setBounds(mixStartX + (sliderSize + spacing), mixSection.getY(), sliderSize, sliderSize);
    earlySendSlider.setBounds(mixStartX + (sliderSize + spacing) * 2, mixSection.getY(), sliderSize, sliderSize);
    lateLevelSlider.setBounds(mixStartX + (sliderSize + spacing) * 3, mixSection.getY(), sliderSize, sliderSize);

    // === Basic Controls Section ===
    auto basicSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
    basicSection.removeFromTop(labelHeight);

    int basicStartX = (basicSection.getWidth() - (sliderSize * 5 + spacing * 4)) / 2;
    sizeSlider.setBounds(basicStartX, basicSection.getY(), sliderSize, sliderSize);
    widthSlider.setBounds(basicStartX + (sliderSize + spacing), basicSection.getY(), sliderSize, sliderSize);
    preDelaySlider.setBounds(basicStartX + (sliderSize + spacing) * 2, basicSection.getY(), sliderSize, sliderSize);
    decaySlider.setBounds(basicStartX + (sliderSize + spacing) * 3, basicSection.getY(), sliderSize, sliderSize);
    diffuseSlider.setBounds(basicStartX + (sliderSize + spacing) * 4, basicSection.getY(), sliderSize, sliderSize);

    // === Modulation & Filter Controls Section ===
    auto modSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
    modSection.removeFromTop(labelHeight);

    int modStartX = (modSection.getWidth() - (sliderSize * 4 + spacing * 3)) / 2;
    spinSlider.setBounds(modStartX, modSection.getY(), sliderSize, sliderSize);
    wanderSlider.setBounds(modStartX + (sliderSize + spacing), modSection.getY(), sliderSize, sliderSize);
    highCutSlider.setBounds(modStartX + (sliderSize + spacing) * 2, modSection.getY(), sliderSize, sliderSize);
    lowCutSlider.setBounds(modStartX + (sliderSize + spacing) * 3, modSection.getY(), sliderSize, sliderSize);

    // === Hall-specific Crossover Controls (only show for Hall algorithm) ===
    if (audioProcessor.reverbType && audioProcessor.reverbType->getIndex() == 1) // Hall
    {
        auto crossSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
        crossSection.removeFromTop(labelHeight);

        int crossStartX = (crossSection.getWidth() - (sliderSize * 4 + spacing * 3)) / 2;
        lowCrossSlider.setBounds(crossStartX, crossSection.getY(), sliderSize, sliderSize);
        highCrossSlider.setBounds(crossStartX + (sliderSize + spacing), crossSection.getY(), sliderSize, sliderSize);
        lowMultSlider.setBounds(crossStartX + (sliderSize + spacing) * 2, crossSection.getY(), sliderSize, sliderSize);
        highMultSlider.setBounds(crossStartX + (sliderSize + spacing) * 3, crossSection.getY(), sliderSize, sliderSize);

        lowCrossSlider.setVisible(true);
        highCrossSlider.setVisible(true);
        lowMultSlider.setVisible(true);
        highMultSlider.setVisible(true);
        lowCrossLabel.setVisible(true);
        highCrossLabel.setVisible(true);
        lowMultLabel.setVisible(true);
        highMultLabel.setVisible(true);
    }
    else
    {
        lowCrossSlider.setVisible(false);
        highCrossSlider.setVisible(false);
        lowMultSlider.setVisible(false);
        highMultSlider.setVisible(false);
        lowCrossLabel.setVisible(false);
        highCrossLabel.setVisible(false);
        lowMultLabel.setVisible(false);
        highMultLabel.setVisible(false);
    }
}

void StudioReverbAudioProcessorEditor::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &reverbTypeCombo)
    {
        // Use the combo box's selected ID directly instead of the parameter value
        int selectedIndex = reverbTypeCombo.getSelectedId() - 1;  // ComboBox IDs start at 1
        DBG("Reverb Type Changed - ComboBox SelectedID: " << reverbTypeCombo.getSelectedId()
            << ", Algorithm Index: " << selectedIndex
            << ", Text: " << reverbTypeCombo.getText());

        updateHallControlsVisibility();
        updatePresetListForAlgorithm(selectedIndex);  // Pass the index directly
        resized();
    }
    else if (comboBoxThatHasChanged == &presetCombo)
    {
        juce::String selectedPreset = presetCombo.getText();
        if (selectedPreset != "-- Select Preset --" && !selectedPreset.isEmpty())
        {
            // Get the algorithm index from the combo box, not the parameter
            int algorithmIndex = reverbTypeCombo.getSelectedId() - 1;
            DBG("Loading preset: " << selectedPreset << " for algorithm " << algorithmIndex);
            audioProcessor.loadPresetForAlgorithm(selectedPreset, algorithmIndex);
        }
    }
}

void StudioReverbAudioProcessorEditor::updateHallControlsVisibility()
{
    bool isHall = (audioProcessor.reverbType && audioProcessor.reverbType->getIndex() == 1);

    lowCrossSlider.setVisible(isHall);
    highCrossSlider.setVisible(isHall);
    lowMultSlider.setVisible(isHall);
    highMultSlider.setVisible(isHall);
    lowCrossLabel.setVisible(isHall);
    highCrossLabel.setVisible(isHall);
    lowMultLabel.setVisible(isHall);
    highMultLabel.setVisible(isHall);

    repaint();
}

void StudioReverbAudioProcessorEditor::updatePresetList()
{
    // This is called on initialization - get index from parameter
    int algorithmIndex = audioProcessor.reverbType ? audioProcessor.reverbType->getIndex() : 0;
    updatePresetListForAlgorithm(algorithmIndex);
}

void StudioReverbAudioProcessorEditor::updatePresetListForAlgorithm(int algorithmIndex)
{
    presetCombo.clear();

    DBG("UpdatePresetListForAlgorithm - Algorithm Index: " << algorithmIndex);

    auto presetNames = audioProcessor.presetManager.getPresetNames(algorithmIndex);
    DBG("UpdatePresetListForAlgorithm - Got " << presetNames.size() << " presets for algorithm " << algorithmIndex);

    for (int i = 0; i < juce::jmin(5, presetNames.size()); ++i)
    {
        DBG("  Preset " << i << ": " << presetNames[i]);
    }

    presetCombo.addItemList(presetNames, 1);
    presetCombo.setSelectedId(1, juce::dontSendNotification);
}
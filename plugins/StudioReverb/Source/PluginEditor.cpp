#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../../../shared/LunaLookAndFeel.h"

// StudioReverb specific customization
class StudioReverbLookAndFeel : public LunaLookAndFeel
{
public:
    StudioReverbLookAndFeel()
    {
        // Inherits Luna color scheme
        // Use larger default font for combo boxes
        setDefaultSansSerifTypeface(juce::Font(juce::FontOptions(14.0f)).getTypefacePtr());
    }

    // Override to make combo box text larger
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int,
                      juce::ComboBox& box) override
    {
        LunaLookAndFeel::drawComboBox(g, width, height, false, 0, 0, 0, 0, box);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(16.0f));  // Larger font for combo box items
    }
};

//==============================================================================
StudioReverbAudioProcessorEditor::StudioReverbAudioProcessorEditor (StudioReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Set custom look and feel
    lookAndFeel = std::make_unique<StudioReverbLookAndFeel>();
    setLookAndFeel(lookAndFeel.get());

    // Unified sizing across all Luna plugins
    setSize(800, 600);
    setResizable(true, true);
    setResizeLimits(600, 450, 1200, 900);

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
    reverbTypeLabel.setFont(juce::Font(16.0f, juce::Font::bold));  // Larger font
    reverbTypeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));  // Brighter text
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
    presetLabel.setFont(juce::Font(16.0f, juce::Font::bold));  // Larger font
    presetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));  // Brighter text
    presetLabel.attachToComponent(&presetCombo, false);

    // === Mix Controls - Separate Dry and Wet ===
    setupSlider(dryLevelSlider, dryLevelLabel, "Dry Level", 1);
    dryLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "dryLevel", dryLevelSlider);

    setupSlider(wetLevelSlider, wetLevelLabel, "Wet Level", 1);
    wetLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "wetLevel", wetLevelSlider);

    // Early controls (for Room and Hall only)
    setupSlider(earlyLevelSlider, earlyLevelLabel, "Early", 1);
    earlyLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "earlyLevel", earlyLevelSlider);

    setupSlider(earlySendSlider, earlySendLabel, "Early Send", 1);
    earlySendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "earlySend", earlySendSlider);

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

    setupSlider(modulationSlider, modulationLabel, "Modulation", 1);
    modulationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "modulation", modulationSlider);

    // === Filter Controls ===
    setupSlider(highCutSlider, highCutLabel, "High Cut", 0);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "highCut", highCutSlider);

    setupSlider(lowCutSlider, lowCutLabel, "Low Cut", 0);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "lowCut", lowCutSlider);

    setupSlider(dampenSlider, dampenLabel, "Dampen", 0);
    dampenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "dampen", dampenSlider);

    setupSlider(earlyDampSlider, earlyDampLabel, "Early Damp", 0);
    earlyDampAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "earlyDamp", earlyDampSlider);

    setupSlider(lateDampSlider, lateDampLabel, "Late Damp", 0);
    lateDampAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "lateDamp", lateDampSlider);

    // === Room-specific Boost Controls ===
    setupSlider(lowBoostSlider, lowBoostLabel, "Low Boost", 0);
    lowBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "lowBoost", lowBoostSlider);

    setupSlider(boostFreqSlider, boostFreqLabel, "Boost Freq", 0);
    boostFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "boostFreq", boostFreqSlider);

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

    // Set initial control visibility based on current reverb type
    currentReverbIndex = audioProcessor.reverbType ? audioProcessor.reverbType->getIndex() : 0;
    reverbTypeCombo.setSelectedId(currentReverbIndex + 1, juce::dontSendNotification);  // Set combo box to match
    updateHallControlsVisibility(currentReverbIndex);
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

    // Mix Control Section
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.setFont(11.0f);
    g.drawText("MIX", bounds.removeFromTop(15), juce::Justification::centred, false);

    auto mixArea = bounds.removeFromTop(sliderSize + 25);
    g.setColour(juce::Colour(0x15ffffff));
    g.fillRoundedRectangle(mixArea.toFloat(), 6.0f);

    // Basic Controls Section
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.drawText("REVERB CHARACTER", bounds.removeFromTop(15), juce::Justification::centred, false);

    auto basicArea = bounds.removeFromTop(sliderSize + 25);
    g.setColour(juce::Colour(0x15ffffff));
    g.fillRoundedRectangle(basicArea.toFloat(), 6.0f);

    // Get current reverb type
    int reverbIndex = currentReverbIndex;
    bool isRoom = (reverbIndex == 0);
    bool isHall = (reverbIndex == 1);
    bool isPlate = (reverbIndex == 2);
    bool isEarlyOnly = (reverbIndex == 3);

    // Mode-specific sections
    if (isEarlyOnly)
    {
        // Early Reflections: Just filters
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("FILTERS", bounds.removeFromTop(15), juce::Justification::centred, false);
        auto filterArea = bounds.removeFromTop(sliderSize + 25);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(filterArea.toFloat(), 6.0f);
    }
    else if (isPlate)
    {
        // Plate: Filters
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("FILTERS", bounds.removeFromTop(15), juce::Justification::centred, false);
        auto filterArea = bounds.removeFromTop(sliderSize + 25);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(filterArea.toFloat(), 6.0f);
    }
    else if (isHall)
    {
        // Hall: Modulation
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("MODULATION", bounds.removeFromTop(15), juce::Justification::centred, false);
        auto modArea = bounds.removeFromTop(sliderSize + 25);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(modArea.toFloat(), 6.0f);

        // Hall: Filters & Crossover
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("FILTERS & CROSSOVER", bounds.removeFromTop(15), juce::Justification::centred, false);
        auto filterArea = bounds.removeFromTop(sliderSize + 25);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(filterArea.toFloat(), 6.0f);
    }
    else if (isRoom)
    {
        // Room: Modulation
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("MODULATION", bounds.removeFromTop(15), juce::Justification::centred, false);
        auto modArea = bounds.removeFromTop(sliderSize + 25);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(modArea.toFloat(), 6.0f);

        // Room: Filters & Damping
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("FILTERS & DAMPING", bounds.removeFromTop(15), juce::Justification::centred, false);
        auto filterArea = bounds.removeFromTop(sliderSize + 25);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(filterArea.toFloat(), 6.0f);

        // Room: Boost Controls
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("BOOST", bounds.removeFromTop(15), juce::Justification::centred, false);
        auto boostArea = bounds.removeFromTop(sliderSize + 25);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(boostArea.toFloat(), 6.0f);
    }
}

void StudioReverbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50); // Title area
    bounds.reduce(20, 20);

    // Reverb Type and Preset Selectors - horizontal layout
    auto selectorArea = bounds.removeFromTop(80);  // Reduced from 120
    selectorArea.removeFromTop(30); // Label space

    // Split horizontally for both selectors
    auto halfWidth = selectorArea.getWidth() / 2;

    // Type selector on left
    auto typeArea = selectorArea.removeFromLeft(halfWidth - 10);
    reverbTypeCombo.setBounds(typeArea.reduced(40, 8));
    reverbTypeCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));

    // Preset selector on right
    auto presetArea = selectorArea.removeFromRight(halfWidth - 10);
    presetCombo.setBounds(presetArea.reduced(40, 8));
    presetCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));

    bounds.removeFromTop(10); // Spacing

    const int sliderSize = 75;
    const int labelHeight = 20;
    const int spacing = 10;

    // Get reverb type to determine layout
    int reverbIndex = currentReverbIndex;  // Use the editor's tracked index
    bool isRoom = (reverbIndex == 0);
    bool isHall = (reverbIndex == 1);
    bool isPlate = (reverbIndex == 2);
    bool isEarlyOnly = (reverbIndex == 3);

    // === Mix Control Section ===
    auto mixSection = bounds.removeFromTop(sliderSize + labelHeight + 25);
    mixSection.removeFromTop(labelHeight);

    if (isRoom || isHall)
    {
        // Room/Hall: Show dry, wet, early level, and early send
        int mixStartX = (mixSection.getWidth() - (sliderSize * 4 + spacing * 3)) / 2;
        dryLevelSlider.setBounds(mixStartX, mixSection.getY(), sliderSize, sliderSize);
        wetLevelSlider.setBounds(mixStartX + (sliderSize + spacing), mixSection.getY(), sliderSize, sliderSize);
        earlyLevelSlider.setBounds(mixStartX + (sliderSize + spacing) * 2, mixSection.getY(), sliderSize, sliderSize);
        earlySendSlider.setBounds(mixStartX + (sliderSize + spacing) * 3, mixSection.getY(), sliderSize, sliderSize);
    }
    else
    {
        // Plate/Early Reflections: Show dry and wet levels
        int mixStartX = (mixSection.getWidth() - (sliderSize * 2 + spacing)) / 2;
        dryLevelSlider.setBounds(mixStartX, mixSection.getY(), sliderSize, sliderSize);
        wetLevelSlider.setBounds(mixStartX + (sliderSize + spacing), mixSection.getY(), sliderSize, sliderSize);
    }

    // === Basic Controls Section ===
    auto basicSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
    basicSection.removeFromTop(labelHeight);

    if (isEarlyOnly)
    {
        // Early Reflections: Only Size and Width
        int basicStartX = (basicSection.getWidth() - (sliderSize * 2 + spacing)) / 2;
        sizeSlider.setBounds(basicStartX, basicSection.getY(), sliderSize, sliderSize);
        widthSlider.setBounds(basicStartX + (sliderSize + spacing), basicSection.getY(), sliderSize, sliderSize);
    }
    else if (isPlate)
    {
        // Plate: Width, PreDelay, Decay (no Size, no Diffuse)
        int basicStartX = (basicSection.getWidth() - (sliderSize * 3 + spacing * 2)) / 2;
        widthSlider.setBounds(basicStartX, basicSection.getY(), sliderSize, sliderSize);
        preDelaySlider.setBounds(basicStartX + (sliderSize + spacing), basicSection.getY(), sliderSize, sliderSize);
        decaySlider.setBounds(basicStartX + (sliderSize + spacing) * 2, basicSection.getY(), sliderSize, sliderSize);
    }
    else
    {
        // Room/Hall: All basic controls
        int basicStartX = (basicSection.getWidth() - (sliderSize * 5 + spacing * 4)) / 2;
        sizeSlider.setBounds(basicStartX, basicSection.getY(), sliderSize, sliderSize);
        widthSlider.setBounds(basicStartX + (sliderSize + spacing), basicSection.getY(), sliderSize, sliderSize);
        preDelaySlider.setBounds(basicStartX + (sliderSize + spacing) * 2, basicSection.getY(), sliderSize, sliderSize);
        decaySlider.setBounds(basicStartX + (sliderSize + spacing) * 3, basicSection.getY(), sliderSize, sliderSize);
        diffuseSlider.setBounds(basicStartX + (sliderSize + spacing) * 4, basicSection.getY(), sliderSize, sliderSize);
    }

    // === Mode-specific layout for remaining controls ===
    if (isEarlyOnly)
    {
        // Early Reflections: Only filters (low cut, high cut)
        auto filterSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
        filterSection.removeFromTop(labelHeight);

        int filterStartX = (filterSection.getWidth() - (sliderSize * 2 + spacing)) / 2;
        lowCutSlider.setBounds(filterStartX, filterSection.getY(), sliderSize, sliderSize);
        highCutSlider.setBounds(filterStartX + (sliderSize + spacing), filterSection.getY(), sliderSize, sliderSize);
    }
    else if (isPlate)
    {
        // Plate: Filters (low cut, high cut, dampen)
        auto filterSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
        filterSection.removeFromTop(labelHeight);

        int filterStartX = (filterSection.getWidth() - (sliderSize * 3 + spacing * 2)) / 2;
        lowCutSlider.setBounds(filterStartX, filterSection.getY(), sliderSize, sliderSize);
        highCutSlider.setBounds(filterStartX + (sliderSize + spacing), filterSection.getY(), sliderSize, sliderSize);
        dampenSlider.setBounds(filterStartX + (sliderSize + spacing) * 2, filterSection.getY(), sliderSize, sliderSize);
    }
    else if (isHall)
    {
        // Hall: Modulation (diffuse, modulation, spin, wander)
        auto modSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
        modSection.removeFromTop(labelHeight);

        int modStartX = (modSection.getWidth() - (sliderSize * 4 + spacing * 3)) / 2;
        // Note: diffuse is already placed in basic controls
        modulationSlider.setBounds(modStartX, modSection.getY(), sliderSize, sliderSize);
        spinSlider.setBounds(modStartX + (sliderSize + spacing), modSection.getY(), sliderSize, sliderSize);
        wanderSlider.setBounds(modStartX + (sliderSize + spacing) * 2, modSection.getY(), sliderSize, sliderSize);
        // 4th position left empty for layout balance

        // Hall: Filters and Crossover (high cut, high cross, high mult, low cut, low cross, low mult)
        auto filterSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
        filterSection.removeFromTop(labelHeight);

        int filterStartX = (filterSection.getWidth() - (sliderSize * 6 + spacing * 5)) / 2;
        highCutSlider.setBounds(filterStartX, filterSection.getY(), sliderSize, sliderSize);
        highCrossSlider.setBounds(filterStartX + (sliderSize + spacing), filterSection.getY(), sliderSize, sliderSize);
        highMultSlider.setBounds(filterStartX + (sliderSize + spacing) * 2, filterSection.getY(), sliderSize, sliderSize);
        lowCutSlider.setBounds(filterStartX + (sliderSize + spacing) * 3, filterSection.getY(), sliderSize, sliderSize);
        lowCrossSlider.setBounds(filterStartX + (sliderSize + spacing) * 4, filterSection.getY(), sliderSize, sliderSize);
        lowMultSlider.setBounds(filterStartX + (sliderSize + spacing) * 5, filterSection.getY(), sliderSize, sliderSize);
    }
    else if (isRoom)
    {
        // Room: Modulation (spin, wander)
        auto modSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
        modSection.removeFromTop(labelHeight);

        int modStartX = (modSection.getWidth() - (sliderSize * 2 + spacing)) / 2;
        spinSlider.setBounds(modStartX, modSection.getY(), sliderSize, sliderSize);
        wanderSlider.setBounds(modStartX + (sliderSize + spacing), modSection.getY(), sliderSize, sliderSize);

        // Room: Filters and Damping (high cut, early damp, late damp, low cut)
        auto filterSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
        filterSection.removeFromTop(labelHeight);

        int filterStartX = (filterSection.getWidth() - (sliderSize * 4 + spacing * 3)) / 2;
        highCutSlider.setBounds(filterStartX, filterSection.getY(), sliderSize, sliderSize);
        earlyDampSlider.setBounds(filterStartX + (sliderSize + spacing), filterSection.getY(), sliderSize, sliderSize);
        lateDampSlider.setBounds(filterStartX + (sliderSize + spacing) * 2, filterSection.getY(), sliderSize, sliderSize);
        lowCutSlider.setBounds(filterStartX + (sliderSize + spacing) * 3, filterSection.getY(), sliderSize, sliderSize);

        // Room: Boost controls (low boost, boost freq)
        auto boostSection = bounds.removeFromTop(sliderSize + labelHeight + 15);
        boostSection.removeFromTop(labelHeight);

        int boostStartX = (boostSection.getWidth() - (sliderSize * 2 + spacing)) / 2;
        lowBoostSlider.setBounds(boostStartX, boostSection.getY(), sliderSize, sliderSize);
        boostFreqSlider.setBounds(boostStartX + (sliderSize + spacing), boostSection.getY(), sliderSize, sliderSize);
    }
}

void StudioReverbAudioProcessorEditor::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &reverbTypeCombo)
    {
        // Use the combo box's selected ID directly instead of the parameter value
        currentReverbIndex = reverbTypeCombo.getSelectedId() - 1;  // ComboBox IDs start at 1
        DBG("Reverb Type Changed - ComboBox SelectedID: " << reverbTypeCombo.getSelectedId()
            << ", Algorithm Index: " << currentReverbIndex
            << ", Text: " << reverbTypeCombo.getText());

        updateHallControlsVisibility(currentReverbIndex);  // Pass the index directly
        updatePresetListForAlgorithm(currentReverbIndex);  // Pass the index directly
        resized();
    }
    else if (comboBoxThatHasChanged == &presetCombo)
    {
        juce::String selectedPreset = presetCombo.getText();
        if (selectedPreset != "-- Select Preset --" && !selectedPreset.isEmpty())
        {
            // Get the algorithm index from the current tracked value
            int algorithmIndex = currentReverbIndex;
            DBG("Loading preset: " << selectedPreset << " for algorithm " << algorithmIndex);
            audioProcessor.loadPresetForAlgorithm(selectedPreset, algorithmIndex);
        }
    }
}

void StudioReverbAudioProcessorEditor::updateHallControlsVisibility(int reverbIndex)
{
    // Use passed index directly

    // Reverb types: 0=Room, 1=Hall, 2=Plate, 3=Early Reflections
    bool isRoom = (reverbIndex == 0);
    bool isHall = (reverbIndex == 1);
    bool isPlate = (reverbIndex == 2);
    bool isEarlyOnly = (reverbIndex == 3);

    // === EARLY REFLECTIONS MODE ===
    // dry/wet, Size, width, low cut, high cut
    if (isEarlyOnly)
    {
        // Mix controls
        dryLevelSlider.setVisible(true);
        dryLevelLabel.setVisible(true);
        wetLevelSlider.setVisible(true);
        wetLevelLabel.setVisible(true);
        earlyLevelSlider.setVisible(false);
        earlyLevelLabel.setVisible(false);
        earlySendSlider.setVisible(false);
        earlySendLabel.setVisible(false);

        // Basic controls
        sizeSlider.setVisible(true);
        sizeLabel.setVisible(true);
        widthSlider.setVisible(true);
        widthLabel.setVisible(true);
        preDelaySlider.setVisible(false);
        preDelayLabel.setVisible(false);
        decaySlider.setVisible(false);
        decayLabel.setVisible(false);
        diffuseSlider.setVisible(false);
        diffuseLabel.setVisible(false);

        // Modulation
        spinSlider.setVisible(false);
        spinLabel.setVisible(false);
        wanderSlider.setVisible(false);
        wanderLabel.setVisible(false);
        modulationSlider.setVisible(false);
        modulationLabel.setVisible(false);

        // Filters
        highCutSlider.setVisible(true);
        highCutLabel.setVisible(true);
        lowCutSlider.setVisible(true);
        lowCutLabel.setVisible(true);
        dampenSlider.setVisible(false);
        dampenLabel.setVisible(false);
        earlyDampSlider.setVisible(false);
        earlyDampLabel.setVisible(false);
        lateDampSlider.setVisible(false);
        lateDampLabel.setVisible(false);

        // Room boost
        lowBoostSlider.setVisible(false);
        lowBoostLabel.setVisible(false);
        boostFreqSlider.setVisible(false);
        boostFreqLabel.setVisible(false);

        // Hall crossovers
        lowCrossSlider.setVisible(false);
        highCrossSlider.setVisible(false);
        lowMultSlider.setVisible(false);
        highMultSlider.setVisible(false);
        lowCrossLabel.setVisible(false);
        highCrossLabel.setVisible(false);
        lowMultLabel.setVisible(false);
        highMultLabel.setVisible(false);
    }
    // === PLATE MODE ===
    // dry/wet, width, pre delay, decay, low cut, high cut, dampen
    else if (isPlate)
    {
        // Mix controls
        dryLevelSlider.setVisible(true);
        dryLevelLabel.setVisible(true);
        wetLevelSlider.setVisible(true);
        wetLevelLabel.setVisible(true);
        earlyLevelSlider.setVisible(false);
        earlyLevelLabel.setVisible(false);
        earlySendSlider.setVisible(false);
        earlySendLabel.setVisible(false);

        // Basic controls
        sizeSlider.setVisible(false);
        sizeLabel.setVisible(false);
        widthSlider.setVisible(true);
        widthLabel.setVisible(true);
        preDelaySlider.setVisible(true);
        preDelayLabel.setVisible(true);
        decaySlider.setVisible(true);
        decayLabel.setVisible(true);
        diffuseSlider.setVisible(false);
        diffuseLabel.setVisible(false);

        // Modulation
        spinSlider.setVisible(false);
        spinLabel.setVisible(false);
        wanderSlider.setVisible(false);
        wanderLabel.setVisible(false);
        modulationSlider.setVisible(false);
        modulationLabel.setVisible(false);

        // Filters
        highCutSlider.setVisible(true);
        highCutLabel.setVisible(true);
        lowCutSlider.setVisible(true);
        lowCutLabel.setVisible(true);
        dampenSlider.setVisible(true);
        dampenLabel.setVisible(true);
        earlyDampSlider.setVisible(false);
        earlyDampLabel.setVisible(false);
        lateDampSlider.setVisible(false);
        lateDampLabel.setVisible(false);

        // Room boost
        lowBoostSlider.setVisible(false);
        lowBoostLabel.setVisible(false);
        boostFreqSlider.setVisible(false);
        boostFreqLabel.setVisible(false);

        // Hall crossovers
        lowCrossSlider.setVisible(false);
        highCrossSlider.setVisible(false);
        lowMultSlider.setVisible(false);
        highMultSlider.setVisible(false);
        lowCrossLabel.setVisible(false);
        highCrossLabel.setVisible(false);
        lowMultLabel.setVisible(false);
        highMultLabel.setVisible(false);
    }
    // === HALL MODE ===
    // wet/dry, early level, early send, size, width, pre delay, decay, diffuse, modulation, spin, wander,
    // high cut, high cross, high mult, low cut, low cross, low mult
    else if (isHall)
    {
        // Mix controls
        dryLevelSlider.setVisible(true);
        dryLevelLabel.setVisible(true);
        wetLevelSlider.setVisible(true);
        wetLevelLabel.setVisible(true);
        earlyLevelSlider.setVisible(true);
        earlyLevelLabel.setVisible(true);
        earlySendSlider.setVisible(true);
        earlySendLabel.setVisible(true);

        // Basic controls
        sizeSlider.setVisible(true);
        sizeLabel.setVisible(true);
        widthSlider.setVisible(true);
        widthLabel.setVisible(true);
        preDelaySlider.setVisible(true);
        preDelayLabel.setVisible(true);
        decaySlider.setVisible(true);
        decayLabel.setVisible(true);
        diffuseSlider.setVisible(true);
        diffuseLabel.setVisible(true);

        // Modulation
        spinSlider.setVisible(true);
        spinLabel.setVisible(true);
        wanderSlider.setVisible(true);
        wanderLabel.setVisible(true);
        modulationSlider.setVisible(true);
        modulationLabel.setVisible(true);

        // Filters
        highCutSlider.setVisible(true);
        highCutLabel.setVisible(true);
        lowCutSlider.setVisible(true);
        lowCutLabel.setVisible(true);
        dampenSlider.setVisible(false);
        dampenLabel.setVisible(false);
        earlyDampSlider.setVisible(false);
        earlyDampLabel.setVisible(false);
        lateDampSlider.setVisible(false);
        lateDampLabel.setVisible(false);

        // Room boost
        lowBoostSlider.setVisible(false);
        lowBoostLabel.setVisible(false);
        boostFreqSlider.setVisible(false);
        boostFreqLabel.setVisible(false);

        // Hall crossovers
        lowCrossSlider.setVisible(true);
        highCrossSlider.setVisible(true);
        lowMultSlider.setVisible(true);
        highMultSlider.setVisible(true);
        lowCrossLabel.setVisible(true);
        highCrossLabel.setVisible(true);
        lowMultLabel.setVisible(true);
        highMultLabel.setVisible(true);
    }
    // === ROOM MODE ===
    // wet/dry, early level, early send, size, width, pre delay, decay, diffuse, spin, wander,
    // high cut, early damp, late damp, low cut, low boost, boost freq
    else if (isRoom)
    {
        // Mix controls
        dryLevelSlider.setVisible(true);
        dryLevelLabel.setVisible(true);
        wetLevelSlider.setVisible(true);
        wetLevelLabel.setVisible(true);
        earlyLevelSlider.setVisible(true);
        earlyLevelLabel.setVisible(true);
        earlySendSlider.setVisible(true);
        earlySendLabel.setVisible(true);

        // Basic controls
        sizeSlider.setVisible(true);
        sizeLabel.setVisible(true);
        widthSlider.setVisible(true);
        widthLabel.setVisible(true);
        preDelaySlider.setVisible(true);
        preDelayLabel.setVisible(true);
        decaySlider.setVisible(true);
        decayLabel.setVisible(true);
        diffuseSlider.setVisible(true);
        diffuseLabel.setVisible(true);

        // Modulation
        spinSlider.setVisible(true);
        spinLabel.setVisible(true);
        wanderSlider.setVisible(true);
        wanderLabel.setVisible(true);
        modulationSlider.setVisible(false);
        modulationLabel.setVisible(false);

        // Filters
        highCutSlider.setVisible(true);
        highCutLabel.setVisible(true);
        lowCutSlider.setVisible(true);
        lowCutLabel.setVisible(true);
        dampenSlider.setVisible(false);
        dampenLabel.setVisible(false);
        earlyDampSlider.setVisible(true);
        earlyDampLabel.setVisible(true);
        lateDampSlider.setVisible(true);
        lateDampLabel.setVisible(true);

        // Room boost
        lowBoostSlider.setVisible(true);
        lowBoostLabel.setVisible(true);
        boostFreqSlider.setVisible(true);
        boostFreqLabel.setVisible(true);

        // Hall crossovers
        lowCrossSlider.setVisible(false);
        highCrossSlider.setVisible(false);
        lowMultSlider.setVisible(false);
        highMultSlider.setVisible(false);
        lowCrossLabel.setVisible(false);
        highCrossLabel.setVisible(false);
        lowMultLabel.setVisible(false);
        highMultLabel.setVisible(false);
    }

    repaint();
}

void StudioReverbAudioProcessorEditor::updatePresetList()
{
    // This is called on initialization - use the tracked index
    updatePresetListForAlgorithm(currentReverbIndex);
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
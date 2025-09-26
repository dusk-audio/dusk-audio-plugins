#include "PluginProcessor.h"
#include "PluginEditor.h"

// StudioReverb specific customization
class StudioReverbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioReverbLookAndFeel()
    {
        // Dark color scheme for StudioReverb
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1a1a1a));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff909090));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffe0e0e0));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff404040));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff808080));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff404040));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xffe0e0e0));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffffffff));  // Bright white text
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff2a2a2a));  // Dark background
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff404040));  // Subtle outline
        setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
    }

    // Override to make combo box text larger
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override
    {
        g.fillAll(box.findColour(juce::ComboBox::backgroundColourId));

        const auto arrowZone = juce::Rectangle<int>(buttonX, buttonY, buttonW, buttonH);
        juce::Path path;
        path.startNewSubPath(arrowZone.getX() + 3.0f, arrowZone.getCentreY() - 2.0f);
        path.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
        path.lineTo(arrowZone.getRight() - 3.0f, arrowZone.getCentreY() - 2.0f);

        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(isButtonDown ? 0.9f : 0.6f));
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(16.0f));  // Larger font for combo box items
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        auto radius = juce::jmin(width / 2, height / 2) - 8.0f;
        auto centreX = x + width * 0.5f;
        auto centreY = y + height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Vintage knob body with gradient
        juce::ColourGradient gradient(juce::Colour(0xff4a4a4a), centreX - radius * 0.5f, centreY - radius * 0.5f,
                                      juce::Colour(0xff1a1a1a), centreX + radius * 0.5f, centreY + radius * 0.5f,
                                      true);
        g.setGradientFill(gradient);
        g.fillEllipse(rx, ry, rw, rw);

        // Vintage-style silver ring
        g.setColour(juce::Colour(0xff606060));
        g.drawEllipse(rx, ry, rw, rw, 2.0f);

        // Inner shadow for depth
        g.setColour(juce::Colour(0x40000000));
        g.drawEllipse(rx + 2, ry + 2, rw - 4, rw - 4, 1.5f);

        // Pointer line - thick vintage style
        juce::Path pointer;
        auto pointerLength = radius * 0.6f;
        auto pointerThickness = 3.0f;
        pointer.addRectangle(-pointerThickness * 0.5f, -radius * 0.9f, pointerThickness, pointerLength);

        // Add a bright dot at the end of the pointer for visibility
        pointer.addEllipse(-2.5f, -radius * 0.85f, 5.0f, 5.0f);

        g.setColour(juce::Colour(0xffe0e0e0));
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        // Center cap with vintage feel
        auto capRadius = radius * 0.25f;
        juce::ColourGradient capGradient(juce::Colour(0xff5a5a5a), centreX - capRadius * 0.5f, centreY - capRadius * 0.5f,
                                         juce::Colour(0xff2a2a2a), centreX + capRadius * 0.5f, centreY + capRadius * 0.5f,
                                         true);
        g.setGradientFill(capGradient);
        g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2.0f, capRadius * 2.0f);

        // Highlight on cap
        g.setColour(juce::Colour(0x30ffffff));
        g.fillEllipse(centreX - capRadius * 0.7f, centreY - capRadius * 0.7f, capRadius * 0.5f, capRadius * 0.5f);

        // Scale marks around the knob
        g.setColour(juce::Colour(0xff808080));
        auto numMarks = 11;
        for (int i = 0; i < numMarks; ++i)
        {
            auto markAngle = rotaryStartAngle + (i * (rotaryEndAngle - rotaryStartAngle) / (numMarks - 1));
            auto markRadius = radius + 5.0f;
            auto markLength = (i == 0 || i == numMarks - 1 || i == (numMarks - 1) / 2) ? 5.0f : 3.0f;

            auto x1 = centreX + std::cos(markAngle) * markRadius;
            auto y1 = centreY + std::sin(markAngle) * markRadius;
            auto x2 = centreX + std::cos(markAngle) * (markRadius + markLength);
            auto y2 = centreY + std::sin(markAngle) * (markRadius + markLength);

            g.drawLine(x1, y1, x2, y2, (i == 0 || i == numMarks - 1 || i == (numMarks - 1) / 2) ? 2.0f : 1.0f);
        }
    }
};

//==============================================================================
StudioReverbAudioProcessorEditor::StudioReverbAudioProcessorEditor (StudioReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Set custom look and feel
    lookAndFeel = std::make_unique<StudioReverbLookAndFeel>();
    setLookAndFeel(lookAndFeel.get());

    // Calculate required size based on max controls (Hall mode has the most)
    // Compact layout for better usability
    // Title: 60, Selector: 80, controls fit within remaining space
    setSize(750, 580);
    setResizable(false, false);

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

    // Plate Type Selector (for Plate reverb only)
    addAndMakeVisible(plateTypeCombo);
    plateTypeCombo.addItemList({"Simple", "Nested", "Tank"}, 1);
    plateTypeCombo.setJustificationType(juce::Justification::centred);
    plateTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "plateType", plateTypeCombo);

    addAndMakeVisible(plateTypeLabel);
    plateTypeLabel.setText("Plate Type", juce::dontSendNotification);
    plateTypeLabel.setJustificationType(juce::Justification::centred);
    plateTypeLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    plateTypeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
    plateTypeLabel.attachToComponent(&plateTypeCombo, false);

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

    setupSlider(wetLevelSlider, wetLevelLabel, "Late Level", 1);  // Match Dragonfly's terminology
    wetLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "lateLevel", wetLevelSlider);  // Connect to lateLevel parameter

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
    auto titleBounds = getLocalBounds().removeFromTop(60);
    juce::ColourGradient titleGradient(
        juce::Colour(0xff2a2a2a), 0, 0,
        juce::Colour(0xff1a1a1a), 0, titleBounds.getHeight(), false);
    g.setGradientFill(titleGradient);
    g.fillRect(titleBounds);

    // Title text
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(32.0f, juce::Font::bold));
    g.drawText("StudioReverb", titleBounds.removeFromBottom(35), juce::Justification::centred, true);

    // Company name
    g.setFont(juce::Font(12.0f));
    g.setColour(juce::Colours::grey);
    g.drawText("Luna Co. Audio", titleBounds,
               juce::Justification::centred, true);

    // Draw section backgrounds - use exact same bounds as resized()
    auto bounds = getLocalBounds();
    bounds.removeFromTop(60);  // Match title height
    bounds.reduce(25, 10);  // Consistent padding

    // Type and Preset selector section
    auto selectorArea = bounds.removeFromTop(75);
    g.setColour(juce::Colour(0x20ffffff));
    g.fillRoundedRectangle(selectorArea.toFloat(), 8.0f);
    g.setColour(juce::Colour(0x40ffffff));
    g.drawRoundedRectangle(selectorArea.toFloat(), 8.0f, 1.0f);

    bounds.removeFromTop(10);  // Spacing between sections

    const int sliderSize = 80;
    const int sectionHeight = sliderSize + 40;  // Include padding for labels

    // Mix Control Section
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.setFont(11.0f);
    g.drawText("MIX", bounds.removeFromTop(20), juce::Justification::centred, false);

    auto mixArea = bounds.removeFromTop(sectionHeight);
    g.setColour(juce::Colour(0x15ffffff));
    g.fillRoundedRectangle(mixArea.toFloat(), 6.0f);

    bounds.removeFromTop(10);  // Spacing

    // Basic Controls Section
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.drawText("REVERB CHARACTER", bounds.removeFromTop(20), juce::Justification::centred, false);

    auto basicArea = bounds.removeFromTop(sectionHeight);
    g.setColour(juce::Colour(0x15ffffff));
    g.fillRoundedRectangle(basicArea.toFloat(), 6.0f);

    bounds.removeFromTop(10);  // Spacing

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
        g.drawText("FILTERS", bounds.removeFromTop(20), juce::Justification::centred, false);
        auto filterArea = bounds.removeFromTop(sectionHeight);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(filterArea.toFloat(), 6.0f);
    }
    else if (isPlate)
    {
        // Plate: Filters
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("FILTERS", bounds.removeFromTop(20), juce::Justification::centred, false);
        auto filterArea = bounds.removeFromTop(sectionHeight);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(filterArea.toFloat(), 6.0f);
    }
    else if (isHall)
    {
        // Hall: Modulation
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("MODULATION", bounds.removeFromTop(20), juce::Justification::centred, false);
        auto modArea = bounds.removeFromTop(sectionHeight);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(modArea.toFloat(), 6.0f);

        bounds.removeFromTop(10);  // Spacing

        // Hall: Filters & Crossover
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("FILTERS & CROSSOVER", bounds.removeFromTop(20), juce::Justification::centred, false);
        auto filterArea = bounds.removeFromTop(sectionHeight);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(filterArea.toFloat(), 6.0f);
    }
    else if (isRoom)
    {
        // Room: Modulation
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("MODULATION", bounds.removeFromTop(20), juce::Justification::centred, false);
        auto modArea = bounds.removeFromTop(sectionHeight);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(modArea.toFloat(), 6.0f);

        bounds.removeFromTop(10);  // Spacing

        // Room: Filters & Damping
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("FILTERS & DAMPING", bounds.removeFromTop(20), juce::Justification::centred, false);
        auto filterArea = bounds.removeFromTop(sectionHeight);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(filterArea.toFloat(), 6.0f);

        bounds.removeFromTop(10);  // Spacing

        // Room: Boost Controls
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawText("BOOST", bounds.removeFromTop(20), juce::Justification::centred, false);
        auto boostArea = bounds.removeFromTop(sectionHeight);
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(boostArea.toFloat(), 6.0f);
    }
}

void StudioReverbAudioProcessorEditor::resized()
{
    // Always start with fresh bounds calculation
    auto bounds = getLocalBounds();
    bounds.removeFromTop(60); // Title area - match paint()
    bounds.reduce(25, 10);  // Match paint() padding

    // Reverb Type and Preset Selectors - horizontal layout
    auto selectorArea = bounds.removeFromTop(75);  // Reduced height
    selectorArea.removeFromTop(25); // Label space

    // Get reverb type to determine if we need plateType combo
    // Use currentReverbIndex for consistency with the rest of the UI
    bool showPlateType = (currentReverbIndex == 2);  // Index 2 is Plate

    if (showPlateType)
    {
        // Three-way split for Plate mode: Type | Plate Type | Preset
        auto thirdWidth = selectorArea.getWidth() / 3;

        // Type selector on left
        auto typeArea = selectorArea.removeFromLeft(thirdWidth - 5);
        reverbTypeCombo.setBounds(typeArea.reduced(30, 8));

        // Plate Type in middle - position it and make visible
        auto plateArea = selectorArea.removeFromLeft(thirdWidth - 5);
        plateTypeCombo.setBounds(plateArea.reduced(30, 8));

        // Preset selector on right
        auto presetArea = selectorArea;
        presetCombo.setBounds(presetArea.reduced(30, 8));
    }
    else
    {
        // Two-way split for other modes
        auto halfWidth = selectorArea.getWidth() / 2;

        // Type selector on left
        auto typeArea = selectorArea.removeFromLeft(halfWidth - 10);
        reverbTypeCombo.setBounds(typeArea.reduced(40, 8));

        // Preset selector on right
        auto presetArea = selectorArea.removeFromRight(halfWidth - 10);
        presetCombo.setBounds(presetArea.reduced(40, 8));
    }

    // Set visibility based on whether it's Plate mode
    plateTypeCombo.setVisible(showPlateType);
    plateTypeLabel.setVisible(showPlateType);

    reverbTypeCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    plateTypeCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    presetCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));

    bounds.removeFromTop(5); // Minimal spacing between sections

    const int sliderSize = 70;  // Compact size for visibility
    const int spacing = 10;  // Tighter spacing between knobs
    const int sectionHeight = sliderSize + 30;  // Compact section height

    // Get reverb type to determine layout - use currentReverbIndex for consistency
    int reverbIndex = currentReverbIndex;
    bool isRoom = (reverbIndex == 0);
    bool isHall = (reverbIndex == 1);
    bool isPlate = (reverbIndex == 2);
    bool isEarlyOnly = (reverbIndex == 3);

    // === Mix Control Section ===
    bounds.removeFromTop(15);  // Section label space
    auto mixSection = bounds.removeFromTop(sectionHeight);
    auto mixKnobArea = mixSection.reduced(10, 5);  // Small padding inside section

    if (isRoom || isHall)
    {
        // Room/Hall: 4 mix controls
        int mixStartX = (mixKnobArea.getWidth() - (sliderSize * 4 + spacing * 3)) / 2;
        int yPos = mixKnobArea.getY() + (mixKnobArea.getHeight() - sliderSize) / 2;
        dryLevelSlider.setBounds(mixKnobArea.getX() + mixStartX, yPos, sliderSize, sliderSize);
        wetLevelSlider.setBounds(mixKnobArea.getX() + mixStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
        earlyLevelSlider.setBounds(mixKnobArea.getX() + mixStartX + (sliderSize + spacing) * 2, yPos, sliderSize, sliderSize);
        earlySendSlider.setBounds(mixKnobArea.getX() + mixStartX + (sliderSize + spacing) * 3, yPos, sliderSize, sliderSize);
    }
    else
    {
        // Plate/Early: 2 mix controls
        int mixStartX = (mixKnobArea.getWidth() - (sliderSize * 2 + spacing)) / 2;
        int yPos = mixKnobArea.getY() + (mixKnobArea.getHeight() - sliderSize) / 2;
        dryLevelSlider.setBounds(mixKnobArea.getX() + mixStartX, yPos, sliderSize, sliderSize);
        wetLevelSlider.setBounds(mixKnobArea.getX() + mixStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
    }

    bounds.removeFromTop(5);  // Spacing

    // === Basic Controls Section ===
    bounds.removeFromTop(15);  // Section label space
    auto basicSection = bounds.removeFromTop(sectionHeight);
    auto basicKnobArea = basicSection.reduced(10, 5);

    if (isEarlyOnly)
    {
        // Early Reflections: Size, Width only
        int basicStartX = (basicKnobArea.getWidth() - (sliderSize * 2 + spacing)) / 2;
        int yPos = basicKnobArea.getY() + (basicKnobArea.getHeight() - sliderSize) / 2;
        sizeSlider.setBounds(basicKnobArea.getX() + basicStartX, yPos, sliderSize, sliderSize);
        widthSlider.setBounds(basicKnobArea.getX() + basicStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
    }
    else if (isPlate)
    {
        // Plate: Width, PreDelay, Decay
        int basicStartX = (basicKnobArea.getWidth() - (sliderSize * 3 + spacing * 2)) / 2;
        int yPos = basicKnobArea.getY() + (basicKnobArea.getHeight() - sliderSize) / 2;
        widthSlider.setBounds(basicKnobArea.getX() + basicStartX, yPos, sliderSize, sliderSize);
        preDelaySlider.setBounds(basicKnobArea.getX() + basicStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
        decaySlider.setBounds(basicKnobArea.getX() + basicStartX + (sliderSize + spacing) * 2, yPos, sliderSize, sliderSize);
    }
    else
    {
        // Room/Hall: All basic controls
        int basicStartX = (basicKnobArea.getWidth() - (sliderSize * 5 + spacing * 4)) / 2;
        int yPos = basicKnobArea.getY() + (basicKnobArea.getHeight() - sliderSize) / 2;
        sizeSlider.setBounds(basicKnobArea.getX() + basicStartX, yPos, sliderSize, sliderSize);
        widthSlider.setBounds(basicKnobArea.getX() + basicStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
        preDelaySlider.setBounds(basicKnobArea.getX() + basicStartX + (sliderSize + spacing) * 2, yPos, sliderSize, sliderSize);
        decaySlider.setBounds(basicKnobArea.getX() + basicStartX + (sliderSize + spacing) * 3, yPos, sliderSize, sliderSize);
        diffuseSlider.setBounds(basicKnobArea.getX() + basicStartX + (sliderSize + spacing) * 4, yPos, sliderSize, sliderSize);
    }

    bounds.removeFromTop(5);  // Spacing

    // === Mode-specific sections ===
    if (isEarlyOnly)
    {
        // Early Reflections: Just filters
        bounds.removeFromTop(15);  // Section label space
        auto filterSection = bounds.removeFromTop(sectionHeight);
        auto filterKnobArea = filterSection.reduced(10, 5);

        int filterStartX = (filterKnobArea.getWidth() - (sliderSize * 2 + spacing)) / 2;
        int yPos = filterKnobArea.getY() + (filterKnobArea.getHeight() - sliderSize) / 2;
        lowCutSlider.setBounds(filterKnobArea.getX() + filterStartX, yPos, sliderSize, sliderSize);
        highCutSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);

    }
    else if (isPlate)
    {
        // Plate: Filters - all three in one row
        bounds.removeFromTop(15);  // Section label space
        auto filterSection = bounds.removeFromTop(sectionHeight);
        auto filterKnobArea = filterSection.reduced(10, 5);

        int filterStartX = (filterKnobArea.getWidth() - (sliderSize * 3 + spacing * 2)) / 2;
        int yPos = filterKnobArea.getY() + (filterKnobArea.getHeight() - sliderSize) / 2;
        dampenSlider.setBounds(filterKnobArea.getX() + filterStartX, yPos, sliderSize, sliderSize);
        lowCutSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
        highCutSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing) * 2, yPos, sliderSize, sliderSize);

    }
    else if (isHall)
    {
        // Hall: Modulation
        bounds.removeFromTop(15);  // Section label space
        auto modSection = bounds.removeFromTop(sectionHeight);
        auto modKnobArea = modSection.reduced(10, 5);

        int modStartX = (modKnobArea.getWidth() - (sliderSize * 3 + spacing * 2)) / 2;
        int yPos = modKnobArea.getY() + (modKnobArea.getHeight() - sliderSize) / 2;
        modulationSlider.setBounds(modKnobArea.getX() + modStartX, yPos, sliderSize, sliderSize);
        spinSlider.setBounds(modKnobArea.getX() + modStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
        wanderSlider.setBounds(modKnobArea.getX() + modStartX + (sliderSize + spacing) * 2, yPos, sliderSize, sliderSize);

        bounds.removeFromTop(5);  // Spacing

        // Hall: Filters & Crossover
        bounds.removeFromTop(15);  // Section label space
        auto filterSection = bounds.removeFromTop(sectionHeight);
        auto filterKnobArea = filterSection.reduced(10, 5);

        int filterStartX = (filterKnobArea.getWidth() - (sliderSize * 6 + spacing * 5)) / 2;
        yPos = filterKnobArea.getY() + (filterKnobArea.getHeight() - sliderSize) / 2;
        highCutSlider.setBounds(filterKnobArea.getX() + filterStartX, yPos, sliderSize, sliderSize);
        highCrossSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
        highMultSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing) * 2, yPos, sliderSize, sliderSize);
        lowCutSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing) * 3, yPos, sliderSize, sliderSize);
        lowCrossSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing) * 4, yPos, sliderSize, sliderSize);
        lowMultSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing) * 5, yPos, sliderSize, sliderSize);
    }
    else if (isRoom)
    {
        // Room: Modulation
        bounds.removeFromTop(15);  // Section label space
        auto modSection = bounds.removeFromTop(sectionHeight);
        auto modKnobArea = modSection.reduced(10, 5);

        int modStartX = (modKnobArea.getWidth() - (sliderSize * 2 + spacing)) / 2;
        int yPos = modKnobArea.getY() + (modKnobArea.getHeight() - sliderSize) / 2;
        spinSlider.setBounds(modKnobArea.getX() + modStartX, yPos, sliderSize, sliderSize);
        wanderSlider.setBounds(modKnobArea.getX() + modStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);

        bounds.removeFromTop(5);  // Spacing

        // Room: Filters & Damping
        bounds.removeFromTop(15);  // Section label space
        auto filterSection = bounds.removeFromTop(sectionHeight);
        auto filterKnobArea = filterSection.reduced(10, 5);

        int filterStartX = (filterKnobArea.getWidth() - (sliderSize * 4 + spacing * 3)) / 2;
        yPos = filterKnobArea.getY() + (filterKnobArea.getHeight() - sliderSize) / 2;
        highCutSlider.setBounds(filterKnobArea.getX() + filterStartX, yPos, sliderSize, sliderSize);
        earlyDampSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
        lateDampSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing) * 2, yPos, sliderSize, sliderSize);
        lowCutSlider.setBounds(filterKnobArea.getX() + filterStartX + (sliderSize + spacing) * 3, yPos, sliderSize, sliderSize);

        bounds.removeFromTop(5);  // Spacing

        // Room: Boost controls
        bounds.removeFromTop(15);  // Section label space
        auto boostSection = bounds.removeFromTop(sectionHeight);
        auto boostKnobArea = boostSection.reduced(10, 5);

        int boostStartX = (boostKnobArea.getWidth() - (sliderSize * 2 + spacing)) / 2;
        yPos = boostKnobArea.getY() + (boostKnobArea.getHeight() - sliderSize) / 2;
        lowBoostSlider.setBounds(boostKnobArea.getX() + boostStartX, yPos, sliderSize, sliderSize);
        boostFreqSlider.setBounds(boostKnobArea.getX() + boostStartX + (sliderSize + spacing), yPos, sliderSize, sliderSize);
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

        // Update wet level label based on reverb type
        if (currentReverbIndex == 2)  // Plate
        {
            wetLevelLabel.setText("Wet Level", juce::dontSendNotification);  // Match Dragonfly Plate
        }
        else
        {
            wetLevelLabel.setText("Late Level", juce::dontSendNotification);  // Room/Hall use Late
        }

        resized();  // This will handle plateType visibility
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

    // Show plate type dropdown only for Plate reverb
    plateTypeCombo.setVisible(isPlate);
    plateTypeLabel.setVisible(isPlate);

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
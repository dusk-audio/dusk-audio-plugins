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
    setResizeLimits(500, 450, 900, 600);
    setSize (750, 500);

    // Reverb Type Selector with improved styling
    addAndMakeVisible(reverbTypeCombo);
    reverbTypeCombo.addItemList({"Early Reflections", "Room", "Plate", "Hall"}, 1);
    reverbTypeCombo.setSelectedId(audioProcessor.reverbType->getIndex() + 1);
    reverbTypeCombo.onChange = [this] {
        audioProcessor.reverbType->setValueNotifyingHost((reverbTypeCombo.getSelectedId() - 1) / 3.0f);
    };
    reverbTypeCombo.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(reverbTypeLabel);
    reverbTypeLabel.setText("Reverb Type", juce::dontSendNotification);
    reverbTypeLabel.setJustificationType(juce::Justification::centred);
    reverbTypeLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    reverbTypeLabel.attachToComponent(&reverbTypeCombo, false);

    // Setup all sliders with consistent styling
    setupSlider(roomSizeSlider, roomSizeLabel, "Room Size", "%", 1);
    roomSizeSlider.setRange(0.0, 100.0, 0.1);
    roomSizeSlider.setValue(audioProcessor.roomSize->get());
    roomSizeSlider.onValueChange = [this] {
        audioProcessor.roomSize->setValueNotifyingHost(roomSizeSlider.getValue() / 100.0f);
    };

    setupSlider(dampingSlider, dampingLabel, "Damping", "%", 1);
    dampingSlider.setRange(0.0, 100.0, 0.1);
    dampingSlider.setValue(audioProcessor.damping->get());
    dampingSlider.onValueChange = [this] {
        audioProcessor.damping->setValueNotifyingHost(dampingSlider.getValue() / 100.0f);
    };

    setupSlider(preDelaySlider, preDelayLabel, "Pre-Delay", "ms", 1);
    preDelaySlider.setRange(0.0, 200.0, 0.1);
    preDelaySlider.setValue(audioProcessor.preDelay->get());
    preDelaySlider.onValueChange = [this] {
        audioProcessor.preDelay->setValueNotifyingHost(preDelaySlider.getValue() / 200.0f);
    };

    // Decay Time with TWO decimal places
    setupSlider(decayTimeSlider, decayTimeLabel, "Decay Time", "s", 2);
    decayTimeSlider.setRange(0.1, 30.0, 0.01);  // Smaller step size for more precision
    decayTimeSlider.setValue(audioProcessor.decayTime->get());
    decayTimeSlider.onValueChange = [this] {
        audioProcessor.decayTime->setValueNotifyingHost((decayTimeSlider.getValue() - 0.1f) / 29.9f);
    };

    setupSlider(diffusionSlider, diffusionLabel, "Diffusion", "%", 1);
    diffusionSlider.setRange(0.0, 100.0, 0.1);
    diffusionSlider.setValue(audioProcessor.diffusion->get());
    diffusionSlider.onValueChange = [this] {
        audioProcessor.diffusion->setValueNotifyingHost(diffusionSlider.getValue() / 100.0f);
    };

    setupSlider(wetLevelSlider, wetLevelLabel, "Wet", "%", 1);
    wetLevelSlider.setRange(0.0, 100.0, 0.1);
    wetLevelSlider.setValue(audioProcessor.wetLevel->get());
    wetLevelSlider.onValueChange = [this] {
        audioProcessor.wetLevel->setValueNotifyingHost(wetLevelSlider.getValue() / 100.0f);
    };

    setupSlider(dryLevelSlider, dryLevelLabel, "Dry", "%", 1);
    dryLevelSlider.setRange(0.0, 100.0, 0.1);
    dryLevelSlider.setValue(audioProcessor.dryLevel->get());
    dryLevelSlider.onValueChange = [this] {
        audioProcessor.dryLevel->setValueNotifyingHost(dryLevelSlider.getValue() / 100.0f);
    };

    setupSlider(widthSlider, widthLabel, "Width", "%", 1);
    widthSlider.setRange(0.0, 100.0, 0.1);
    widthSlider.setValue(audioProcessor.width->get());
    widthSlider.onValueChange = [this] {
        audioProcessor.width->setValueNotifyingHost(widthSlider.getValue() / 100.0f);
    };

    startTimerHz(30);
}

StudioReverbAudioProcessorEditor::~StudioReverbAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void StudioReverbAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label,
                                                   const juce::String& labelText,
                                                   const juce::String& suffix,
                                                   int decimalPlaces)
{
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    slider.setTextValueSuffix(" " + suffix);
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

    // Type selector section
    auto typeArea = bounds.removeFromTop(70);
    g.setColour(juce::Colour(0x20ffffff));
    g.fillRoundedRectangle(typeArea.toFloat(), 8.0f);
    g.setColour(juce::Colour(0x40ffffff));
    g.drawRoundedRectangle(typeArea.toFloat(), 8.0f, 1.0f);

    // Main parameters section
    bounds.removeFromTop(15);
    g.setColour(juce::Colour(0x15ffffff));
    g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    g.setColour(juce::Colour(0x30ffffff));
    g.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);
}

void StudioReverbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50); // Title area
    bounds.reduce(20, 20);

    // Reverb Type Selector
    auto typeArea = bounds.removeFromTop(70);
    typeArea.removeFromTop(25); // Label space
    reverbTypeCombo.setBounds(typeArea.reduced(120, 8));

    bounds.removeFromTop(25); // Spacing

    const int sliderSize = 85;
    const int labelHeight = 20;
    const int spacing = 15;

    // Calculate layout
    int numColumns = 4;
    int totalWidth = (sliderSize * numColumns) + (spacing * (numColumns - 1));
    int startX = (bounds.getWidth() - totalWidth) / 2;

    // First row - Main reverb parameters
    auto row1 = bounds.removeFromTop(sliderSize + labelHeight + 10);
    row1.removeFromTop(labelHeight);

    roomSizeSlider.setBounds(bounds.getX() + startX, row1.getY(), sliderSize, sliderSize);
    dampingSlider.setBounds(bounds.getX() + startX + (sliderSize + spacing), row1.getY(), sliderSize, sliderSize);
    preDelaySlider.setBounds(bounds.getX() + startX + (sliderSize + spacing) * 2, row1.getY(), sliderSize, sliderSize);
    decayTimeSlider.setBounds(bounds.getX() + startX + (sliderSize + spacing) * 3, row1.getY(), sliderSize, sliderSize);

    // Second row - Mix and modulation parameters
    auto row2 = bounds.removeFromTop(sliderSize + labelHeight + 10);
    row2.removeFromTop(labelHeight);

    diffusionSlider.setBounds(bounds.getX() + startX, row2.getY(), sliderSize, sliderSize);
    wetLevelSlider.setBounds(bounds.getX() + startX + (sliderSize + spacing), row2.getY(), sliderSize, sliderSize);
    dryLevelSlider.setBounds(bounds.getX() + startX + (sliderSize + spacing) * 2, row2.getY(), sliderSize, sliderSize);
    widthSlider.setBounds(bounds.getX() + startX + (sliderSize + spacing) * 3, row2.getY(), sliderSize, sliderSize);
}

void StudioReverbAudioProcessorEditor::timerCallback()
{
    // Update UI values from processor
    if (reverbTypeCombo.getSelectedId() != audioProcessor.reverbType->getIndex() + 1)
        reverbTypeCombo.setSelectedId(audioProcessor.reverbType->getIndex() + 1, juce::dontSendNotification);

    if (!juce::approximatelyEqual(static_cast<float>(roomSizeSlider.getValue()), audioProcessor.roomSize->get()))
        roomSizeSlider.setValue(audioProcessor.roomSize->get(), juce::dontSendNotification);

    if (!juce::approximatelyEqual(static_cast<float>(dampingSlider.getValue()), audioProcessor.damping->get()))
        dampingSlider.setValue(audioProcessor.damping->get(), juce::dontSendNotification);

    if (!juce::approximatelyEqual(static_cast<float>(preDelaySlider.getValue()), audioProcessor.preDelay->get()))
        preDelaySlider.setValue(audioProcessor.preDelay->get(), juce::dontSendNotification);

    if (!juce::approximatelyEqual(static_cast<float>(decayTimeSlider.getValue()), audioProcessor.decayTime->get()))
        decayTimeSlider.setValue(audioProcessor.decayTime->get(), juce::dontSendNotification);

    if (!juce::approximatelyEqual(static_cast<float>(diffusionSlider.getValue()), audioProcessor.diffusion->get()))
        diffusionSlider.setValue(audioProcessor.diffusion->get(), juce::dontSendNotification);

    if (!juce::approximatelyEqual(static_cast<float>(wetLevelSlider.getValue()), audioProcessor.wetLevel->get()))
        wetLevelSlider.setValue(audioProcessor.wetLevel->get(), juce::dontSendNotification);

    if (!juce::approximatelyEqual(static_cast<float>(dryLevelSlider.getValue()), audioProcessor.dryLevel->get()))
        dryLevelSlider.setValue(audioProcessor.dryLevel->get(), juce::dontSendNotification);

    if (!juce::approximatelyEqual(static_cast<float>(widthSlider.getValue()), audioProcessor.width->get()))
        widthSlider.setValue(audioProcessor.width->get(), juce::dontSendNotification);
}
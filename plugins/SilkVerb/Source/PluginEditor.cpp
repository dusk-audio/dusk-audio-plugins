/*
  ==============================================================================

    PluginEditor.cpp
    SilkVerb - Algorithmic Reverb with Plate, Room, Hall modes

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// SilkVerbLookAndFeel implementation
//==============================================================================
SilkVerbLookAndFeel::SilkVerbLookAndFeel()
{
    // Dark theme matching other Luna plugins
    setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1a1a1a));
    setColour(juce::Slider::thumbColourId, juce::Colour(0xff6a9ad9));
    setColour(juce::Slider::trackColourId, juce::Colour(0xff2a2a2a));
    setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
}

void SilkVerbLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                           juce::Slider& slider)
{
    auto radius = juce::jmin(width / 2, height / 2) - 6.0f;
    auto centreX = x + width * 0.5f;
    auto centreY = y + height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Outer shadow
    g.setColour(juce::Colour(0x40000000));
    g.fillEllipse(rx + 2, ry + 2, rw, rw);

    // Knob body with gradient - deep blue/gray
    juce::ColourGradient bodyGradient(
        juce::Colour(0xff3a4550), centreX - radius * 0.7f, centreY - radius * 0.7f,
        juce::Colour(0xff1a2028), centreX + radius * 0.7f, centreY + radius * 0.7f,
        true);
    g.setGradientFill(bodyGradient);
    g.fillEllipse(rx, ry, rw, rw);

    // Outer ring
    g.setColour(juce::Colour(0xff5a6a7a));
    g.drawEllipse(rx, ry, rw, rw, 2.0f);

    // Arc track (background)
    juce::Path arcBg;
    arcBg.addCentredArc(centreX, centreY, radius - 4, radius - 4,
                        0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff2a2a2a));
    g.strokePath(arcBg, juce::PathStrokeType(3.0f));

    // Arc track (value) - silky blue
    if (sliderPos > 0.0f)
    {
        juce::Path arcValue;
        arcValue.addCentredArc(centreX, centreY, radius - 4, radius - 4,
                              0.0f, rotaryStartAngle, angle, true);
        g.setColour(juce::Colour(0xff6a9ad9));
        g.strokePath(arcValue, juce::PathStrokeType(3.0f));
    }

    // Pointer
    juce::Path pointer;
    auto pointerLength = radius * 0.6f;
    auto pointerThickness = 3.0f;

    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -radius + 8,
                                pointerThickness, pointerLength, 1.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    g.setColour(juce::Colour(0xffe0e0e0));
    g.fillPath(pointer);
}

void SilkVerbLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2);
    auto isOn = button.getToggleState();
    bool isFreezeButton = (&button == freezeButtonPtr);

    // Button background
    if (isOn)
    {
        if (isFreezeButton)
        {
            // Freeze active: ice blue glow
            g.setColour(juce::Colour(0xff4fc3f7).withAlpha(0.3f));
            g.fillRoundedRectangle(bounds.expanded(2), 6.0f);

            juce::ColourGradient gradient(
                juce::Colour(0xff29b6f6), bounds.getCentreX(), bounds.getY(),
                juce::Colour(0xff0288d1), bounds.getCentreX(), bounds.getBottom(),
                false);
            g.setGradientFill(gradient);
        }
        else
        {
            // Selected state - silky blue glow
            g.setColour(juce::Colour(0xff6a9ad9).withAlpha(0.2f));
            g.fillRoundedRectangle(bounds.expanded(2), 6.0f);

            juce::ColourGradient gradient(
                juce::Colour(0xff4a7ab9), bounds.getCentreX(), bounds.getY(),
                juce::Colour(0xff3a5a89), bounds.getCentreX(), bounds.getBottom(),
                false);
            g.setGradientFill(gradient);
        }
    }
    else
    {
        juce::ColourGradient gradient(
            juce::Colour(0xff3a3a3a), bounds.getCentreX(), bounds.getY(),
            juce::Colour(0xff2a2a2a), bounds.getCentreX(), bounds.getBottom(),
            false);
        g.setGradientFill(gradient);
    }
    g.fillRoundedRectangle(bounds, 5.0f);

    // Border
    juce::Colour borderColour = isOn ? (isFreezeButton ? juce::Colour(0xff4fc3f7) : juce::Colour(0xff6a9ad9))
                                     : juce::Colour(0xff4a4a4a);
    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds, 5.0f, 1.5f);

    // Highlight on hover
    if (shouldDrawButtonAsHighlighted && !isOn)
    {
        g.setColour(juce::Colour(0x20ffffff));
        g.fillRoundedRectangle(bounds, 5.0f);
    }

    // Text
    g.setColour(isOn ? juce::Colour(0xffffffff) : juce::Colour(0xffa0a0a0));
    g.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred);

    juce::ignoreUnused(shouldDrawButtonAsDown);
}

//==============================================================================
// SilkVerbEditor implementation
//==============================================================================
SilkVerbEditor::SilkVerbEditor(SilkVerbProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Mode buttons (Plate/Room/Hall)
    plateButton.setButtonText("PLATE");
    plateButton.setRadioGroupId(1);
    plateButton.setClickingTogglesState(true);
    plateButton.onClick = [this]() { modeButtonClicked(0); };
    addAndMakeVisible(plateButton);

    roomButton.setButtonText("ROOM");
    roomButton.setRadioGroupId(1);
    roomButton.setClickingTogglesState(true);
    roomButton.onClick = [this]() { modeButtonClicked(1); };
    addAndMakeVisible(roomButton);

    hallButton.setButtonText("HALL");
    hallButton.setRadioGroupId(1);
    hallButton.setClickingTogglesState(true);
    hallButton.onClick = [this]() { modeButtonClicked(2); };
    addAndMakeVisible(hallButton);

    // Color buttons (Modern/Vintage)
    modernButton.setButtonText("MODERN");
    modernButton.setRadioGroupId(2);
    modernButton.setClickingTogglesState(true);
    modernButton.onClick = [this]() { colorButtonClicked(0); };
    addAndMakeVisible(modernButton);

    vintageButton.setButtonText("VINTAGE");
    vintageButton.setRadioGroupId(2);
    vintageButton.setClickingTogglesState(true);
    vintageButton.onClick = [this]() { colorButtonClicked(1); };
    addAndMakeVisible(vintageButton);

    // Freeze button
    freezeButton.setButtonText("FREEZE");
    freezeButton.setClickingTogglesState(true);
    addAndMakeVisible(freezeButton);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "freeze", freezeButton);
    lookAndFeel.setFreezeButton(&freezeButton);

    // Main controls (Row 1)
    setupSlider(sizeSlider, sizeLabel, "SIZE");
    sizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "size", sizeSlider);

    setupSlider(dampingSlider, dampingLabel, "DAMPING");
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "damping", dampingSlider);

    setupSlider(preDelaySlider, preDelayLabel, "PRE-DELAY");
    preDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "predelay", preDelaySlider);

    setupSlider(mixSlider, mixLabel, "MIX");
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mix", mixSlider);

    // Modulation controls (Row 2)
    setupSmallSlider(modRateSlider, modRateLabel, "RATE");
    modRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "modrate", modRateSlider);

    setupSmallSlider(modDepthSlider, modDepthLabel, "DEPTH");
    modDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "moddepth", modDepthSlider);

    setupSmallSlider(widthSlider, widthLabel, "WIDTH");
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "width", widthSlider);

    // Diffusion controls (Row 3 left)
    setupSmallSlider(earlyDiffSlider, earlyDiffLabel, "EARLY");
    earlyDiffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "earlydiff", earlyDiffSlider);

    setupSmallSlider(lateDiffSlider, lateDiffLabel, "LATE");
    lateDiffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "latediff", lateDiffSlider);

    // Bass controls (Row 3 right)
    setupSmallSlider(bassMultSlider, bassMultLabel, "BASS X");
    bassMultAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "bassmult", bassMultSlider);

    setupSmallSlider(bassFreqSlider, bassFreqLabel, "BASS Hz");
    bassFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "bassfreq", bassFreqSlider);

    // EQ controls (Row 4)
    setupSmallSlider(lowCutSlider, lowCutLabel, "LOW CUT");
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowcut", lowCutSlider);

    setupSmallSlider(highCutSlider, highCutLabel, "HIGH CUT");
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highcut", highCutSlider);

    // Initialize buttons to current state
    updateModeButtons();
    updateColorButtons();

    startTimerHz(30);

    setSize(500, 580);
    setResizable(true, true);
    setResizeLimits(450, 520, 700, 800);
}

SilkVerbEditor::~SilkVerbEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void SilkVerbEditor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe0e0e0));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff2a2a2a));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff3a3a3a));
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffa0a0a0));
    label.setFont(juce::Font(juce::FontOptions(11.0f)).withStyle(juce::Font::bold));
    addAndMakeVisible(label);
}

void SilkVerbEditor::setupSmallSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffc0c0c0));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff252525));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff353535));
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    label.setFont(juce::Font(juce::FontOptions(10.0f)).withStyle(juce::Font::bold));
    addAndMakeVisible(label);
}

void SilkVerbEditor::updateModeButtons()
{
    auto* modeParam = audioProcessor.getAPVTS().getRawParameterValue("mode");
    if (modeParam == nullptr)
        return;

    int currentMode = static_cast<int>(modeParam->load());

    plateButton.setToggleState(currentMode == 0, juce::dontSendNotification);
    roomButton.setToggleState(currentMode == 1, juce::dontSendNotification);
    hallButton.setToggleState(currentMode == 2, juce::dontSendNotification);
}

void SilkVerbEditor::modeButtonClicked(int mode)
{
    audioProcessor.getAPVTS().getParameterAsValue("mode").setValue(mode);
    updateModeButtons();
}

void SilkVerbEditor::updateColorButtons()
{
    auto* colorParam = audioProcessor.getAPVTS().getRawParameterValue("color");
    if (colorParam == nullptr)
        return;

    int currentColor = static_cast<int>(colorParam->load());

    modernButton.setToggleState(currentColor == 0, juce::dontSendNotification);
    vintageButton.setToggleState(currentColor == 1, juce::dontSendNotification);
}

void SilkVerbEditor::colorButtonClicked(int color)
{
    audioProcessor.getAPVTS().getParameterAsValue("color").setValue(color);
    updateColorButtons();
}

void SilkVerbEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xff1a1a1a));

    auto bounds = getLocalBounds();

    // Header
    auto headerArea = bounds.removeFromTop(50);
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(headerArea);

    // Title
    g.setFont(juce::Font(juce::FontOptions(22.0f)).withStyle(juce::Font::bold));
    g.setColour(juce::Colour(0xff6a9ad9));
    g.drawText("SilkVerb", headerArea, juce::Justification::centred);

    // Subtitle
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.setColour(juce::Colour(0xff808080));
    g.drawText("Algorithmic Reverb", headerArea.withTrimmedTop(28), juce::Justification::centred);

    // Mode section background
    auto modeArea = bounds.removeFromTop(40);
    modeArea.reduce(10, 5);
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(modeArea.toFloat(), 5.0f);

    // Color section background
    bounds.removeFromTop(5);
    auto colorArea = bounds.removeFromTop(35);
    colorArea.reduce(10, 2);
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(colorArea.toFloat(), 5.0f);

    // Main controls section
    bounds.removeFromTop(8);
    auto mainSection = bounds.removeFromTop(115);
    mainSection.reduce(10, 0);
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(mainSection.toFloat(), 5.0f);

    // Modulation section
    bounds.removeFromTop(8);
    auto modSection = bounds.removeFromTop(95);
    modSection.reduce(10, 0);
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(modSection.toFloat(), 5.0f);

    // Section label
    g.setFont(juce::Font(juce::FontOptions(9.0f)).withStyle(juce::Font::bold));
    g.setColour(juce::Colour(0xff6a9ad9));
    g.drawText("MODULATION", modSection.removeFromTop(14).reduced(8, 0), juce::Justification::centredLeft);

    // Diffusion & Bass section
    bounds.removeFromTop(8);
    auto diffBassSection = bounds.removeFromTop(95);
    diffBassSection.reduce(10, 0);
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(diffBassSection.toFloat(), 5.0f);

    // Section labels - position at 1/4 and 3/4 points
    auto diffBassLabelArea = diffBassSection.removeFromTop(14).reduced(8, 0);
    int quarterWidth = diffBassLabelArea.getWidth() / 4;
    g.setColour(juce::Colour(0xff6a9ad9));
    g.drawText("DIFFUSION", diffBassLabelArea.withWidth(quarterWidth * 2), juce::Justification::centredLeft);
    g.drawText("BASS DECAY", diffBassLabelArea.withX(diffBassLabelArea.getX() + quarterWidth * 2).withWidth(quarterWidth * 2), juce::Justification::centredLeft);

    // EQ section
    bounds.removeFromTop(8);
    auto eqSection = bounds.removeFromTop(95);
    eqSection.reduce(10, 0);
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(eqSection.toFloat(), 5.0f);

    // Section label
    g.setColour(juce::Colour(0xff6a9ad9));
    g.drawText("OUTPUT EQ", eqSection.removeFromTop(14).reduced(8, 0), juce::Justification::centredLeft);

    // Footer
    g.setFont(juce::Font(juce::FontOptions(9.0f)).withStyle(juce::Font::italic));
    g.setColour(juce::Colour(0xff606060));
    g.drawText("Luna Co. Audio", getLocalBounds().removeFromBottom(16), juce::Justification::centred);
}

void SilkVerbEditor::resized()
{
    auto bounds = getLocalBounds();
    const int margin = 15;
    const int sectionGap = 8;

    // Header
    bounds.removeFromTop(50);

    // Mode buttons row
    auto modeRow = bounds.removeFromTop(40);
    modeRow.reduce(20, 8);

    int modeButtonWidth = (modeRow.getWidth() - 20) / 3;
    plateButton.setBounds(modeRow.removeFromLeft(modeButtonWidth));
    modeRow.removeFromLeft(10);
    roomButton.setBounds(modeRow.removeFromLeft(modeButtonWidth));
    modeRow.removeFromLeft(10);
    hallButton.setBounds(modeRow);

    // Color buttons row (Modern/Vintage + Freeze)
    bounds.removeFromTop(5);
    auto colorRow = bounds.removeFromTop(35);
    colorRow.reduce(20, 4);

    int colorButtonWidth = (colorRow.getWidth() - 20) / 3;  // 3 buttons now
    modernButton.setBounds(colorRow.removeFromLeft(colorButtonWidth));
    colorRow.removeFromLeft(10);
    vintageButton.setBounds(colorRow.removeFromLeft(colorButtonWidth));
    colorRow.removeFromLeft(10);
    freezeButton.setBounds(colorRow);

    // Main controls section (Size, Damping, PreDelay, Mix)
    bounds.removeFromTop(sectionGap);
    auto mainSection = bounds.removeFromTop(115);
    mainSection.reduce(margin, 8);

    int mainKnobSize = 65;
    int labelHeight = 14;
    int cellWidth = mainSection.getWidth() / 4;

    // Layout main knobs
    juce::Slider* mainSliders[] = { &sizeSlider, &dampingSlider, &preDelaySlider, &mixSlider };
    juce::Label* mainLabels[] = { &sizeLabel, &dampingLabel, &preDelayLabel, &mixLabel };

    for (int i = 0; i < 4; ++i)
    {
        auto cell = mainSection.withX(mainSection.getX() + i * cellWidth).withWidth(cellWidth);
        mainLabels[i]->setBounds(cell.removeFromTop(labelHeight));
        mainSliders[i]->setBounds(cell.withSizeKeepingCentre(mainKnobSize, mainKnobSize + 18));
    }

    // Modulation section (Rate, Depth, Width)
    bounds.removeFromTop(sectionGap);
    auto modSection = bounds.removeFromTop(95);
    modSection.reduce(margin, 5);
    modSection.removeFromTop(14); // Section label space

    int smallKnobSize = 50;
    int smallLabelHeight = 12;
    int modCellWidth = modSection.getWidth() / 3;

    juce::Slider* modSliders[] = { &modRateSlider, &modDepthSlider, &widthSlider };
    juce::Label* modLabels[] = { &modRateLabel, &modDepthLabel, &widthLabel };

    for (int i = 0; i < 3; ++i)
    {
        auto cell = modSection.withX(modSection.getX() + i * modCellWidth).withWidth(modCellWidth);
        modLabels[i]->setBounds(cell.removeFromTop(smallLabelHeight));
        modSliders[i]->setBounds(cell.withSizeKeepingCentre(smallKnobSize, smallKnobSize + 14));
    }

    // Diffusion & Bass section
    bounds.removeFromTop(sectionGap);
    auto diffBassSection = bounds.removeFromTop(95);
    diffBassSection.reduce(margin, 5);
    diffBassSection.removeFromTop(14); // Section label space

    // Split into 4 equal columns
    int quarterWidth = diffBassSection.getWidth() / 4;

    juce::Slider* diffBassSliders[] = { &earlyDiffSlider, &lateDiffSlider, &bassMultSlider, &bassFreqSlider };
    juce::Label* diffBassLabels[] = { &earlyDiffLabel, &lateDiffLabel, &bassMultLabel, &bassFreqLabel };

    for (int i = 0; i < 4; ++i)
    {
        auto cell = diffBassSection.withX(diffBassSection.getX() + i * quarterWidth).withWidth(quarterWidth);
        diffBassLabels[i]->setBounds(cell.removeFromTop(smallLabelHeight));
        diffBassSliders[i]->setBounds(cell.withSizeKeepingCentre(smallKnobSize, smallKnobSize + 14));
    }

    // EQ section (Low Cut, High Cut)
    bounds.removeFromTop(sectionGap);
    auto eqSection = bounds.removeFromTop(95);
    eqSection.reduce(margin, 5);
    eqSection.removeFromTop(14); // Section label space

    int eqCellWidth = eqSection.getWidth() / 2;

    juce::Slider* eqSliders[] = { &lowCutSlider, &highCutSlider };
    juce::Label* eqLabels[] = { &lowCutLabel, &highCutLabel };

    for (int i = 0; i < 2; ++i)
    {
        auto cell = eqSection.withX(eqSection.getX() + i * eqCellWidth).withWidth(eqCellWidth);
        eqLabels[i]->setBounds(cell.removeFromTop(smallLabelHeight));
        eqSliders[i]->setBounds(cell.withSizeKeepingCentre(smallKnobSize, smallKnobSize + 14));
    }
}

void SilkVerbEditor::timerCallback()
{
    // Update buttons in case parameters changed externally
    updateModeButtons();
    updateColorButtons();
}

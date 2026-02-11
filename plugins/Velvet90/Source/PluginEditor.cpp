/*
  ==============================================================================

    PluginEditor.cpp
    Velvet 90 - Algorithmic Reverb with Plate, Room, Hall modes

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Velvet90LookAndFeel implementation
//==============================================================================
Velvet90LookAndFeel::Velvet90LookAndFeel()
{
    // Dark theme matching other Dusk Audio plugins
    setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1a1a1a));
    setColour(juce::Slider::thumbColourId, juce::Colour(0xff6a9ad9));
    setColour(juce::Slider::trackColourId, juce::Colour(0xff2a2a2a));
    setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
}

void Velvet90LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
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

void Velvet90LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
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
// LCDDisplay implementation — PCM 90-style VFD
//==============================================================================
LCDDisplay::LCDDisplay()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LCDDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Ambient green glow behind the LCD (VFD screen illumination)
    g.setColour(juce::Colour(0x0a00d870));
    g.fillRoundedRectangle(bounds.expanded(3.0f), 7.0f);

    // Outer bezel
    g.setColour(juce::Colour(0xff080808));
    g.fillRoundedRectangle(bounds, 5.0f);

    // Display area
    auto display = bounds.reduced(2.5f);

    // LCD background — very dark with green tint (VFD phosphor look)
    juce::ColourGradient bg(
        juce::Colour(0xff0c1e14), display.getX(), display.getY(),
        juce::Colour(0xff081a10), display.getRight(), display.getBottom(),
        false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(display, 3.0f);

    // Inner shadow at top edge (inset look)
    juce::ColourGradient shadow(
        juce::Colour(0x18000000), display.getX(), display.getY(),
        juce::Colours::transparentBlack, display.getX(), display.getY() + 6.0f,
        false);
    g.setGradientFill(shadow);
    g.fillRoundedRectangle(display, 3.0f);

    // Inner border
    g.setColour(juce::Colour(0xff1a2a1a));
    g.drawRoundedRectangle(display, 3.0f, 1.0f);

    // Scanlines for VFD effect
    g.setColour(juce::Colour(0x06000000));
    for (int y = static_cast<int>(display.getY()); y < static_cast<int>(display.getBottom()); y += 2)
        g.drawHorizontalLine(y, display.getX(), display.getRight());

    // Text areas
    auto textArea = display.reduced(10.0f, 2.0f);
    auto line1Area = textArea.removeFromTop(textArea.getHeight() * 0.45f);
    auto line2Area = textArea;

    juce::Colour textColor(0xff00d870);
    juce::Colour glowColor(0x1800d870);

    auto monoName = juce::Font::getDefaultMonospacedFontName();

    // Line 1 — category:mode (left) and RT60 (right)
    g.setFont(juce::Font(juce::FontOptions(monoName, 10.0f, juce::Font::plain)));
    g.setColour(glowColor);
    g.drawText(line1, line1Area.expanded(1.0f), juce::Justification::centredLeft);
    g.setColour(textColor);
    g.drawText(line1, line1Area, juce::Justification::centredLeft);

    if (line1Right.isNotEmpty())
    {
        g.setColour(glowColor);
        g.drawText(line1Right, line1Area.expanded(1.0f), juce::Justification::centredRight);
        g.setColour(textColor);
        g.drawText(line1Right, line1Area, juce::Justification::centredRight);
    }

    // Line 2 — preset name (larger, bold)
    g.setFont(juce::Font(juce::FontOptions(monoName, 13.0f, juce::Font::bold)));
    g.setColour(glowColor);
    g.drawText(line2, line2Area.expanded(1.0f), juce::Justification::centredLeft);
    g.setColour(textColor);
    g.drawText(line2, line2Area, juce::Justification::centredLeft);
}

void LCDDisplay::mouseDown(const juce::MouseEvent&)
{
    if (onClick) onClick();
}

//==============================================================================
// PresetBrowserOverlay implementation
//==============================================================================
PresetBrowserOverlay::PresetBrowserOverlay(Velvet90Processor& p)
    : processor(p)
{
    auto presets = Velvet90Presets::getFactoryPresets();
    for (const auto& preset : presets)
    {
        if (std::find(categoryOrder.begin(), categoryOrder.end(), preset.category) == categoryOrder.end())
            categoryOrder.push_back(preset.category);
    }
    if (!categoryOrder.empty())
        selectedCategory = categoryOrder[0];
}

void PresetBrowserOverlay::paint(juce::Graphics& g)
{
    // Semi-transparent backdrop
    g.fillAll(juce::Colour(0xd0101010));

    auto panel = getLocalBounds().reduced(20, 35);

    // Panel background
    g.setColour(juce::Colour(0xff1e1e1e));
    g.fillRoundedRectangle(panel.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff6a9ad9));
    g.drawRoundedRectangle(panel.toFloat(), 8.0f, 1.5f);

    // Header
    auto header = panel.removeFromTop(30);
    g.setFont(juce::Font(juce::FontOptions(14.0f)).withStyle(juce::Font::bold));
    g.setColour(juce::Colour(0xff6a9ad9));
    g.drawText("PRESETS", header, juce::Justification::centred);

    // Category tabs
    panel.removeFromTop(2);
    auto tabRow = panel.removeFromTop(26);
    tabRow.reduce(6, 0);
    int numCats = static_cast<int>(categoryOrder.size());
    int tabWidth = numCats > 0 ? tabRow.getWidth() / numCats : 0;

    g.setFont(juce::Font(juce::FontOptions(10.0f)).withStyle(juce::Font::bold));
    for (size_t i = 0; i < categoryOrder.size(); ++i)
    {
        auto tab = tabRow.removeFromLeft(tabWidth);
        bool isSelected = (categoryOrder[i] == selectedCategory);

        if (isSelected)
        {
            g.setColour(juce::Colour(0xff3a5a89));
            g.fillRoundedRectangle(tab.reduced(1).toFloat(), 4.0f);
        }

        g.setColour(isSelected ? juce::Colour(0xffffffff) : juce::Colour(0xff808080));
        g.drawText(categoryOrder[i], tab, juce::Justification::centred);
    }

    // Separator
    panel.removeFromTop(4);
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawHorizontalLine(panel.getY(), static_cast<float>(panel.getX() + 8),
                         static_cast<float>(panel.getRight() - 8));
    panel.removeFromTop(6);

    int currentProg = processor.getCurrentProgram();

    // Init entry
    auto initRow = panel.removeFromTop(22);
    initRow.reduce(10, 0);
    if (currentProg == 0)
    {
        g.setColour(juce::Colour(0xff2a3a4a));
        g.fillRoundedRectangle(initRow.toFloat(), 3.0f);
    }
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(currentProg == 0 ? juce::Colour(0xff6a9ad9) : juce::Colour(0xffb0b0b0));
    g.drawText("Init", initRow.reduced(8, 0), juce::Justification::centredLeft);

    panel.removeFromTop(3);

    // Presets for selected category
    auto presets = Velvet90Presets::getFactoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (presets[i].category != selectedCategory)
            continue;

        auto row = panel.removeFromTop(22);
        if (row.getBottom() > getLocalBounds().reduced(20, 35).getBottom() - 8)
            break;

        row.reduce(10, 0);
        int progIdx = static_cast<int>(i + 1);

        if (currentProg == progIdx)
        {
            g.setColour(juce::Colour(0xff2a3a4a));
            g.fillRoundedRectangle(row.toFloat(), 3.0f);
        }

        g.setColour(currentProg == progIdx ? juce::Colour(0xff6a9ad9) : juce::Colour(0xffc0c0c0));
        g.drawText(presets[i].name, row.reduced(8, 0), juce::Justification::centredLeft);
    }
}

void PresetBrowserOverlay::mouseDown(const juce::MouseEvent& event)
{
    auto panel = getLocalBounds().reduced(20, 35);

    // Click outside panel = dismiss
    if (!panel.contains(event.getPosition()))
    {
        if (onDismiss) onDismiss();
        return;
    }

    // Skip header
    panel.removeFromTop(30);

    // Category tabs
    panel.removeFromTop(2);
    auto tabRow = panel.removeFromTop(26);
    tabRow.reduce(6, 0);
    int numCats = static_cast<int>(categoryOrder.size());
    int tabWidth = numCats > 0 ? tabRow.getWidth() / numCats : 0;

    for (size_t i = 0; i < categoryOrder.size(); ++i)
    {
        auto tab = tabRow.removeFromLeft(tabWidth);
        if (tab.contains(event.getPosition()))
        {
            selectedCategory = categoryOrder[i];
            repaint();
            return;
        }
    }

    // Separator space
    panel.removeFromTop(10);

    // Init entry
    auto initRow = panel.removeFromTop(22);
    initRow.reduce(10, 0);
    if (initRow.contains(event.getPosition()))
    {
        processor.setCurrentProgram(0);
        if (onDismiss) onDismiss();
        return;
    }

    panel.removeFromTop(3);

    // Presets
    auto presets = Velvet90Presets::getFactoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (presets[i].category != selectedCategory)
            continue;

        auto row = panel.removeFromTop(22);
        if (row.getBottom() > getLocalBounds().reduced(20, 35).getBottom() - 8)
            break;

        row.reduce(10, 0);
        if (row.contains(event.getPosition()))
        {
            processor.setCurrentProgram(static_cast<int>(i + 1));
            if (onDismiss) onDismiss();
            return;
        }
    }
}

//==============================================================================
// Velvet90Editor implementation
//==============================================================================
Velvet90Editor::Velvet90Editor(Velvet90Processor& p)
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

    chamberButton.setButtonText("CHAMBER");
    chamberButton.setRadioGroupId(1);
    chamberButton.setClickingTogglesState(true);
    chamberButton.onClick = [this]() { modeButtonClicked(3); };
    addAndMakeVisible(chamberButton);

    cathedralButton.setButtonText("CATHEDRAL");
    cathedralButton.setRadioGroupId(1);
    cathedralButton.setClickingTogglesState(true);
    cathedralButton.onClick = [this]() { modeButtonClicked(4); };
    addAndMakeVisible(cathedralButton);

    ambienceButton.setButtonText("AMBIENCE");
    ambienceButton.setRadioGroupId(1);
    ambienceButton.setClickingTogglesState(true);
    ambienceButton.onClick = [this]() { modeButtonClicked(5); };
    addAndMakeVisible(ambienceButton);

    brightHallButton.setButtonText("BR.HALL");
    brightHallButton.setRadioGroupId(1);
    brightHallButton.setClickingTogglesState(true);
    brightHallButton.onClick = [this]() { modeButtonClicked(6); };
    addAndMakeVisible(brightHallButton);

    chorusButton.setButtonText("CHORUS");
    chorusButton.setRadioGroupId(1);
    chorusButton.setClickingTogglesState(true);
    chorusButton.onClick = [this]() { modeButtonClicked(7); };
    addAndMakeVisible(chorusButton);

    randomButton.setButtonText("RANDOM");
    randomButton.setRadioGroupId(1);
    randomButton.setClickingTogglesState(true);
    randomButton.onClick = [this]() { modeButtonClicked(8); };
    addAndMakeVisible(randomButton);

    dirtyButton.setButtonText("DIRTY");
    dirtyButton.setRadioGroupId(1);
    dirtyButton.setClickingTogglesState(true);
    dirtyButton.onClick = [this]() { modeButtonClicked(9); };
    addAndMakeVisible(dirtyButton);

    // Freeze button
    freezeButton.setButtonText("FREEZE");
    freezeButton.setClickingTogglesState(true);
    addAndMakeVisible(freezeButton);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "freeze", freezeButton);
    lookAndFeel.setFreezeButton(&freezeButton);

    // LED output meter
    outputMeter.setStereoMode(true);
    outputMeter.setRefreshRate(30.0f);
    addAndMakeVisible(outputMeter);

    // Row 1 — Reverb: Size, Pre-Delay, Shape, Spread
    setupSlider(sizeSlider, sizeLabel, "SIZE");
    sizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "size", sizeSlider);

    setupSlider(preDelaySlider, preDelayLabel, "PRE-DELAY");
    preDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "predelay", preDelaySlider);

    setupSlider(shapeSlider, shapeLabel, "SHAPE");
    shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "ershape", shapeSlider);

    setupSlider(spreadSlider, spreadLabel, "SPREAD");
    spreadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "erspread", spreadSlider);

    // Row 2 — Tone: Damping, Bass Boost, HF Decay, Diffusion
    setupSlider(dampingSlider, dampingLabel, "DAMPING");
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "damping", dampingSlider);

    setupSlider(bassBoostSlider, bassBoostLabel, "BASS RT");
    bassBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "bassmult", bassBoostSlider);

    setupSlider(hfDecaySlider, hfDecayLabel, "HF DECAY");
    hfDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highdecay", hfDecaySlider);

    setupSlider(diffusionSlider, diffusionLabel, "DIFFUSION");
    diffusionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "latediff", diffusionSlider);

    // Row 3 — Output: Width, Mix, Low Cut, High Cut
    setupSlider(widthSlider, widthLabel, "WIDTH");
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "width", widthSlider);

    setupSlider(mixSlider, mixLabel, "MIX");
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mix", mixSlider);

    setupSlider(lowCutSlider, lowCutLabel, "LOW CUT");
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowcut", lowCutSlider);

    setupSlider(highCutSlider, highCutLabel, "HIGH CUT");
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highcut", highCutSlider);

    // === Tab 1: DECAY controls ===
    // Row 1 — Room
    setupSlider(roomSizeSlider, roomSizeLabel, "ROOM SIZE");
    roomSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "roomsize", roomSizeSlider);

    setupSlider(earlyDiffSlider, earlyDiffLabel, "EARLY DIFF");
    earlyDiffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "earlydiff", earlyDiffSlider);

    setupSlider(erLateBalSlider, erLateBalLabel, "ER/LATE");
    erLateBalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "erlatebal", erLateBalSlider);

    setupSlider(erBassCutSlider, erBassCutLabel, "ER BASS CUT");
    erBassCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "erbasscut", erBassCutSlider);

    // Row 2 — Frequency
    setupSlider(bassFreqSlider, bassFreqLabel, "BASS FREQ");
    bassFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "bassfreq", bassFreqSlider);

    setupSlider(midDecaySlider, midDecayLabel, "MID DECAY");
    midDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "middecay", midDecaySlider);

    setupSlider(highFreqSlider, highFreqLabel, "HIGH FREQ");
    highFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highfreq", highFreqSlider);

    setupSlider(trebleRatioSlider, trebleRatioLabel, "TREBLE RT");
    trebleRatioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "trebleratio", trebleRatioSlider);

    // Row 3 — Modulation
    setupSlider(lowMidFreqSlider, lowMidFreqLabel, "LO-MID FREQ");
    lowMidFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowmidfreq", lowMidFreqSlider);

    setupSlider(lowMidDecaySlider, lowMidDecayLabel, "LO-MID RT");
    lowMidDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowmiddecay", lowMidDecaySlider);

    setupSlider(modRateSlider, modRateLabel, "MOD RATE");
    modRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "modrate", modRateSlider);

    setupSlider(modDepthSlider, modDepthLabel, "MOD DEPTH");
    modDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "moddepth", modDepthSlider);

    // === Tab 2: EFFECTS controls ===
    // Row 1 — Envelope
    setupLabel(envModeLabel, "ENV MODE");
    envModeBox.addItemList(juce::StringArray{ "Off", "Gate", "Reverse", "Swell", "Ducked" }, 1);
    envModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    envModeBox.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    envModeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a3a3a));
    addAndMakeVisible(envModeBox);
    envModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "envmode", envModeBox);

    setupSlider(envDepthSlider, envDepthLabel, "ENV DEPTH");
    envDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "envdepth", envDepthSlider);

    setupSlider(envHoldSlider, envHoldLabel, "ENV HOLD");
    envHoldAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "envhold", envHoldSlider);

    setupSlider(envReleaseSlider, envReleaseLabel, "ENV RELEASE");
    envReleaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "envrelease", envReleaseSlider);

    // Row 2 — Echo
    setupSlider(echoDelaySlider, echoDelayLabel, "ECHO DELAY");
    echoDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "echodelay", echoDelaySlider);

    setupSlider(echoFeedbackSlider, echoFeedbackLabel, "ECHO FB");
    echoFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "echofeedback", echoFeedbackSlider);

    setupSlider(echoPingPongSlider, echoPingPongLabel, "PING-PONG");
    echoPingPongAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "echopingpong", echoPingPongSlider);

    setupSlider(resonanceSlider, resonanceLabel, "RESONANCE");
    resonanceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "resonance", resonanceSlider);

    // Row 3 — Dynamics
    setupSlider(dynAmountSlider, dynAmountLabel, "DYN AMOUNT");
    dynAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "dynamount", dynAmountSlider);

    setupSlider(dynSpeedSlider, dynSpeedLabel, "DYN SPEED");
    dynSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "dynspeed", dynSpeedSlider);

    setupSlider(stereoCouplingSlider, stereoCouplingLabel, "STEREO CPL");
    stereoCouplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "stereocoupling", stereoCouplingSlider);

    setupSlider(stereoInvertSlider, stereoInvertLabel, "STEREO INV");
    stereoInvertAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "stereoinvert", stereoInvertSlider);

    // === Tab 3: OUTPUT EQ controls ===
    // Row 1 — Band 1
    setupSlider(outEQ1FreqSlider, outEQ1FreqLabel, "EQ1 FREQ");
    outEQ1FreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outeq1freq", outEQ1FreqSlider);

    setupSlider(outEQ1GainSlider, outEQ1GainLabel, "EQ1 GAIN");
    outEQ1GainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outeq1gain", outEQ1GainSlider);

    setupSlider(outEQ1QSlider, outEQ1QLabel, "EQ1 Q");
    outEQ1QAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outeq1q", outEQ1QSlider);

    // Row 2 — Band 2
    setupSlider(outEQ2FreqSlider, outEQ2FreqLabel, "EQ2 FREQ");
    outEQ2FreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outeq2freq", outEQ2FreqSlider);

    setupSlider(outEQ2GainSlider, outEQ2GainLabel, "EQ2 GAIN");
    outEQ2GainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outeq2gain", outEQ2GainSlider);

    setupSlider(outEQ2QSlider, outEQ2QLabel, "EQ2 Q");
    outEQ2QAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outeq2q", outEQ2QSlider);

    // Pre-delay sync controls
    preDelaySyncButton.setButtonText("SYNC");
    preDelaySyncButton.setClickingTogglesState(true);
    addAndMakeVisible(preDelaySyncButton);
    preDelaySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "predelaysync", preDelaySyncButton);

    preDelayNoteBox.addItemList(juce::StringArray{ "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D" }, 1);
    addAndMakeVisible(preDelayNoteBox);
    preDelayNoteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "predelaynote", preDelayNoteBox);

    // Preset navigation — PCM 90-style LCD with prev/next arrows
    prevPresetButton.setButtonText("<");
    prevPresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0a0a0a));
    prevPresetButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0a0a0a));
    prevPresetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff00d870));
    prevPresetButton.onClick = [this]() { navigatePreset(-1); };
    addAndMakeVisible(prevPresetButton);

    nextPresetButton.setButtonText(">");
    nextPresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0a0a0a));
    nextPresetButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0a0a0a));
    nextPresetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff00d870));
    nextPresetButton.onClick = [this]() { navigatePreset(1); };
    addAndMakeVisible(nextPresetButton);

    lcdDisplay.onClick = [this]() { showPresetBrowser(); };
    addAndMakeVisible(lcdDisplay);

    // Tooltips
    sizeSlider.setTooltip(DuskTooltips::withAllHints("Reverb decay time"));
    preDelaySlider.setTooltip(DuskTooltips::withAllHints("Delay before reverb onset"));
    shapeSlider.setTooltip(DuskTooltips::withAllHints("Early reflection envelope (front-loaded to building)"));
    spreadSlider.setTooltip(DuskTooltips::withAllHints("Early reflection spacing (dense to sparse)"));
    dampingSlider.setTooltip(DuskTooltips::withAllHints("High-frequency air absorption"));
    bassBoostSlider.setTooltip(DuskTooltips::withAllHints("Low-frequency decay ratio"));
    hfDecaySlider.setTooltip(DuskTooltips::withAllHints("High-frequency decay ratio"));
    diffusionSlider.setTooltip(DuskTooltips::withAllHints("Late reverb diffusion density"));
    widthSlider.setTooltip(DuskTooltips::withAllHints("Stereo width"));
    mixSlider.setTooltip(DuskTooltips::withAllHints("Dry/wet balance"));
    lowCutSlider.setTooltip(DuskTooltips::withAllHints("Output high-pass filter"));
    highCutSlider.setTooltip(DuskTooltips::withAllHints("Output low-pass filter"));
    freezeButton.setTooltip("Infinite sustain — holds the reverb tail");
    preDelaySyncButton.setTooltip("Sync pre-delay to host tempo");

    // Tab 1 tooltips
    roomSizeSlider.setTooltip(DuskTooltips::withAllHints("FDN room size (delay line scaling)"));
    earlyDiffSlider.setTooltip(DuskTooltips::withAllHints("Early reflection diffusion density"));
    erLateBalSlider.setTooltip(DuskTooltips::withAllHints("Balance between early reflections and late tail"));
    erBassCutSlider.setTooltip(DuskTooltips::withAllHints("High-pass filter on early reflections"));
    bassFreqSlider.setTooltip(DuskTooltips::withAllHints("Bass crossover frequency"));
    midDecaySlider.setTooltip(DuskTooltips::withAllHints("Mid-frequency decay multiplier"));
    highFreqSlider.setTooltip(DuskTooltips::withAllHints("High crossover frequency"));
    trebleRatioSlider.setTooltip(DuskTooltips::withAllHints("Treble decay ratio"));
    lowMidFreqSlider.setTooltip(DuskTooltips::withAllHints("Low-mid crossover frequency"));
    lowMidDecaySlider.setTooltip(DuskTooltips::withAllHints("Low-mid decay multiplier"));
    modRateSlider.setTooltip(DuskTooltips::withAllHints("Chorus modulation rate"));
    modDepthSlider.setTooltip(DuskTooltips::withAllHints("Chorus modulation depth"));

    // Tab 2 tooltips
    envModeBox.setTooltip("Envelope shaper mode (Off, Gate, Reverse, Swell, Ducked)");
    envDepthSlider.setTooltip(DuskTooltips::withAllHints("Envelope shaper depth"));
    envHoldSlider.setTooltip(DuskTooltips::withAllHints("Envelope hold time"));
    envReleaseSlider.setTooltip(DuskTooltips::withAllHints("Envelope release time"));
    echoDelaySlider.setTooltip(DuskTooltips::withAllHints("Post-reverb echo delay"));
    echoFeedbackSlider.setTooltip(DuskTooltips::withAllHints("Echo feedback amount"));
    echoPingPongSlider.setTooltip(DuskTooltips::withAllHints("Cross-channel echo feedback (L-R-L-R bounce)"));
    resonanceSlider.setTooltip(DuskTooltips::withAllHints("Metallic/resonant coloration"));
    dynAmountSlider.setTooltip(DuskTooltips::withAllHints("Sidechain dynamics (negative=duck, positive=expand)"));
    dynSpeedSlider.setTooltip(DuskTooltips::withAllHints("Dynamics envelope follower speed"));
    stereoCouplingSlider.setTooltip(DuskTooltips::withAllHints("Stereo channel coupling"));
    stereoInvertSlider.setTooltip(DuskTooltips::withAllHints("Stereo anti-correlation (wide vintage-style imaging)"));

    // Tab 3 tooltips
    outEQ1FreqSlider.setTooltip(DuskTooltips::withAllHints("Output EQ band 1 frequency"));
    outEQ1GainSlider.setTooltip(DuskTooltips::withAllHints("Output EQ band 1 gain"));
    outEQ1QSlider.setTooltip(DuskTooltips::withAllHints("Output EQ band 1 Q factor"));
    outEQ2FreqSlider.setTooltip(DuskTooltips::withAllHints("Output EQ band 2 frequency"));
    outEQ2GainSlider.setTooltip(DuskTooltips::withAllHints("Output EQ band 2 gain"));
    outEQ2QSlider.setTooltip(DuskTooltips::withAllHints("Output EQ band 2 Q factor"));

    // Initialize buttons to current state
    updateModeButtons();

    // Initialize tab visibility (show MAIN tab, hide others)
    switchTab(0);

    startTimerHz(30);

    // Initialize resizable UI (560x558 base — 530 + 28 for tab bar)
    resizeHelper.initialize(this, &audioProcessor, 560, 558, 460, 488, 720, 708, false);
    setSize(resizeHelper.getStoredWidth(), resizeHelper.getStoredHeight());
}

Velvet90Editor::~Velvet90Editor()
{
    resizeHelper.saveSize();
    stopTimer();
    setLookAndFeel(nullptr);
}

void Velvet90Editor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    // DuskSlider already has Shift+drag fine control built-in
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

void Velvet90Editor::setupLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffa0a0a0));
    label.setFont(juce::Font(juce::FontOptions(11.0f)).withStyle(juce::Font::bold));
    addAndMakeVisible(label);
}

void Velvet90Editor::switchTab(int tab)
{
    currentTab = tab;

    bool showMain = (tab == 0);
    bool showDecay = (tab == 1);
    bool showEffects = (tab == 2);
    bool showOutputEQ = (tab == 3);

    // Tab 0: MAIN
    auto setMainVisible = [&](bool v) {
        sizeSlider.setVisible(v);    sizeLabel.setVisible(v);
        preDelaySlider.setVisible(v); preDelayLabel.setVisible(v);
        shapeSlider.setVisible(v);   shapeLabel.setVisible(v);
        spreadSlider.setVisible(v);  spreadLabel.setVisible(v);
        dampingSlider.setVisible(v); dampingLabel.setVisible(v);
        bassBoostSlider.setVisible(v); bassBoostLabel.setVisible(v);
        hfDecaySlider.setVisible(v); hfDecayLabel.setVisible(v);
        diffusionSlider.setVisible(v); diffusionLabel.setVisible(v);
        widthSlider.setVisible(v);   widthLabel.setVisible(v);
        mixSlider.setVisible(v);     mixLabel.setVisible(v);
        lowCutSlider.setVisible(v);  lowCutLabel.setVisible(v);
        highCutSlider.setVisible(v); highCutLabel.setVisible(v);
        preDelaySyncButton.setVisible(v);
        preDelayNoteBox.setVisible(v);
    };

    // Tab 1: DECAY
    auto setDecayVisible = [&](bool v) {
        roomSizeSlider.setVisible(v);   roomSizeLabel.setVisible(v);
        earlyDiffSlider.setVisible(v);  earlyDiffLabel.setVisible(v);
        erLateBalSlider.setVisible(v);  erLateBalLabel.setVisible(v);
        erBassCutSlider.setVisible(v);  erBassCutLabel.setVisible(v);
        bassFreqSlider.setVisible(v);   bassFreqLabel.setVisible(v);
        midDecaySlider.setVisible(v);   midDecayLabel.setVisible(v);
        highFreqSlider.setVisible(v);   highFreqLabel.setVisible(v);
        trebleRatioSlider.setVisible(v); trebleRatioLabel.setVisible(v);
        lowMidFreqSlider.setVisible(v); lowMidFreqLabel.setVisible(v);
        lowMidDecaySlider.setVisible(v); lowMidDecayLabel.setVisible(v);
        modRateSlider.setVisible(v);    modRateLabel.setVisible(v);
        modDepthSlider.setVisible(v);   modDepthLabel.setVisible(v);
    };

    // Tab 2: EFFECTS
    auto setEffectsVisible = [&](bool v) {
        envModeBox.setVisible(v);       envModeLabel.setVisible(v);
        envDepthSlider.setVisible(v);   envDepthLabel.setVisible(v);
        envHoldSlider.setVisible(v);    envHoldLabel.setVisible(v);
        envReleaseSlider.setVisible(v); envReleaseLabel.setVisible(v);
        echoDelaySlider.setVisible(v);  echoDelayLabel.setVisible(v);
        echoFeedbackSlider.setVisible(v); echoFeedbackLabel.setVisible(v);
        echoPingPongSlider.setVisible(v); echoPingPongLabel.setVisible(v);
        resonanceSlider.setVisible(v);  resonanceLabel.setVisible(v);
        dynAmountSlider.setVisible(v);  dynAmountLabel.setVisible(v);
        dynSpeedSlider.setVisible(v);   dynSpeedLabel.setVisible(v);
        stereoCouplingSlider.setVisible(v); stereoCouplingLabel.setVisible(v);
        stereoInvertSlider.setVisible(v); stereoInvertLabel.setVisible(v);
    };

    // Tab 3: OUTPUT EQ
    auto setOutputEQVisible = [&](bool v) {
        outEQ1FreqSlider.setVisible(v); outEQ1FreqLabel.setVisible(v);
        outEQ1GainSlider.setVisible(v); outEQ1GainLabel.setVisible(v);
        outEQ1QSlider.setVisible(v);    outEQ1QLabel.setVisible(v);
        outEQ2FreqSlider.setVisible(v); outEQ2FreqLabel.setVisible(v);
        outEQ2GainSlider.setVisible(v); outEQ2GainLabel.setVisible(v);
        outEQ2QSlider.setVisible(v);    outEQ2QLabel.setVisible(v);
    };

    setMainVisible(showMain);
    setDecayVisible(showDecay);
    setEffectsVisible(showEffects);
    setOutputEQVisible(showOutputEQ);

    resized();
    repaint();
}

void Velvet90Editor::updateModeButtons()
{
    auto* modeParam = audioProcessor.getAPVTS().getRawParameterValue("mode");
    if (modeParam == nullptr)
        return;

    int currentMode = static_cast<int>(modeParam->load());

    plateButton.setToggleState(currentMode == 0, juce::dontSendNotification);
    roomButton.setToggleState(currentMode == 1, juce::dontSendNotification);
    hallButton.setToggleState(currentMode == 2, juce::dontSendNotification);
    chamberButton.setToggleState(currentMode == 3, juce::dontSendNotification);
    cathedralButton.setToggleState(currentMode == 4, juce::dontSendNotification);
    ambienceButton.setToggleState(currentMode == 5, juce::dontSendNotification);
    brightHallButton.setToggleState(currentMode == 6, juce::dontSendNotification);
    chorusButton.setToggleState(currentMode == 7, juce::dontSendNotification);
    randomButton.setToggleState(currentMode == 8, juce::dontSendNotification);
    dirtyButton.setToggleState(currentMode == 9, juce::dontSendNotification);
}

void Velvet90Editor::modeButtonClicked(int mode)
{
    audioProcessor.getAPVTS().getParameterAsValue("mode").setValue(mode);
    updateModeButtons();
}

void Velvet90Editor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xff1a1a1a));

    auto bounds = getLocalBounds();

    // Header (title row + LCD row)
    auto headerArea = bounds.removeFromTop(66);
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(headerArea);

    // Title (clickable for supporters) — top portion of header
    auto titleRow = headerArea.withHeight(24);
    titleClickArea = titleRow.withWidth(120).withX(titleRow.getX() + 10);

    g.setFont(juce::Font(juce::FontOptions(18.0f)).withStyle(juce::Font::bold));
    g.setColour(juce::Colour(0xff6a9ad9));
    g.drawText("Velvet 90", titleRow.reduced(12, 0), juce::Justification::centredLeft);

    // Reserve right side for meter
    bounds.removeFromRight(30);

    // Mode section background (2 rows, compact)
    auto modeArea = bounds.removeFromTop(56);
    modeArea.reduce(8, 3);
    g.setColour(juce::Colour(0xff232323));
    g.fillRoundedRectangle(modeArea.toFloat(), 5.0f);

    // === Tab bar (28px) ===
    auto tabArea = bounds.removeFromTop(28);
    tabArea.reduce(8, 2);
    static const char* tabNames[] = { "MAIN", "DECAY", "EFFECTS", "OUTPUT EQ" };
    int tabWidth = tabArea.getWidth() / 4;
    g.setFont(juce::Font(juce::FontOptions(10.0f)).withStyle(juce::Font::bold));
    for (int i = 0; i < 4; ++i)
    {
        auto tab = tabArea.withX(tabArea.getX() + i * tabWidth).withWidth(tabWidth);
        if (i == currentTab)
        {
            g.setColour(juce::Colour(0xff3a5a89));
            g.fillRoundedRectangle(tab.reduced(2).toFloat(), 4.0f);
        }
        g.setColour(i == currentTab ? juce::Colour(0xffffffff) : juce::Colour(0xff808080));
        g.drawText(tabNames[i], tab, juce::Justification::centred);
    }

    // === Section backgrounds (tab-dependent) ===
    // Section label names per tab
    static const char* sectionLabels[][3] = {
        { "REVERB", "TONE", "OUTPUT" },         // Tab 0: MAIN
        { "ROOM", "FREQUENCY", "MODULATION" },   // Tab 1: DECAY
        { "ENVELOPE", "ECHO", "DYNAMICS" },        // Tab 2: EFFECTS (3 rows)
        { "BAND 1", "BAND 2", nullptr },         // Tab 3: OUTPUT EQ (2 rows)
    };
    int numSections = (currentTab == 3) ? 2 : 3;

    auto paintSection = [&](juce::Rectangle<int>& area, const char* label)
    {
        area.removeFromTop(5);
        auto section = area.removeFromTop(110);
        section.reduce(8, 0);
        g.setColour(juce::Colour(0xff262626));
        g.fillRoundedRectangle(section.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff2e2e2e));
        g.drawHorizontalLine(section.getY() + 1,
                             static_cast<float>(section.getX() + 5),
                             static_cast<float>(section.getRight() - 5));
        g.setFont(juce::Font(juce::FontOptions(9.0f)).withStyle(juce::Font::bold));
        g.setColour(juce::Colour(0xff6a9ad9));
        g.drawText(label, section.removeFromTop(14).reduced(10, 0), juce::Justification::centredLeft);
    };

    for (int i = 0; i < numSections; ++i)
        paintSection(bounds, sectionLabels[currentTab][i]);

    // Footer
    g.setFont(juce::Font(juce::FontOptions(9.0f)).withStyle(juce::Font::italic));
    g.setColour(juce::Colour(0xff606060));
    g.drawText("Dusk Audio", getLocalBounds().removeFromBottom(14), juce::Justification::centred);
}

void Velvet90Editor::resized()
{
    resizeHelper.updateResizer();

    auto bounds = getLocalBounds();

    // Header (66px — title row + LCD row)
    auto headerArea = bounds.removeFromTop(66);

    // Freeze button in title row (right of "Velvet 90", left of center)
    freezeButton.setBounds(140, 1, 80, 22);

    // LCD display and prev/next buttons in lower header
    auto lcdRow = headerArea.withTop(24).withHeight(40).reduced(16, 0);
    prevPresetButton.setBounds(lcdRow.removeFromLeft(24));
    nextPresetButton.setBounds(lcdRow.removeFromRight(24));
    lcdDisplay.setBounds(lcdRow.reduced(3, 0));

    // Reserve right side for LED meter
    auto meterStrip = bounds.removeFromRight(30);

    // Mode buttons (2 rows of 5, compact)
    auto modeSection = bounds.removeFromTop(56);
    modeSection.reduce(12, 3);

    int modeButtonGap = 3;
    auto modeRow1 = modeSection.removeFromTop(modeSection.getHeight() / 2).reduced(0, 1);
    auto modeRow2 = modeSection.reduced(0, 1);

    int modeButtonWidth = (modeRow1.getWidth() - modeButtonGap * 4) / 5;
    // Row 1: Plate, Room, Hall, Br.Hall, Chamber
    plateButton.setBounds(modeRow1.removeFromLeft(modeButtonWidth));
    modeRow1.removeFromLeft(modeButtonGap);
    roomButton.setBounds(modeRow1.removeFromLeft(modeButtonWidth));
    modeRow1.removeFromLeft(modeButtonGap);
    hallButton.setBounds(modeRow1.removeFromLeft(modeButtonWidth));
    modeRow1.removeFromLeft(modeButtonGap);
    brightHallButton.setBounds(modeRow1.removeFromLeft(modeButtonWidth));
    modeRow1.removeFromLeft(modeButtonGap);
    chamberButton.setBounds(modeRow1);

    // Row 2: Cathedral, Ambience, Chorus, Random, Dirty
    cathedralButton.setBounds(modeRow2.removeFromLeft(modeButtonWidth));
    modeRow2.removeFromLeft(modeButtonGap);
    ambienceButton.setBounds(modeRow2.removeFromLeft(modeButtonWidth));
    modeRow2.removeFromLeft(modeButtonGap);
    chorusButton.setBounds(modeRow2.removeFromLeft(modeButtonWidth));
    modeRow2.removeFromLeft(modeButtonGap);
    randomButton.setBounds(modeRow2.removeFromLeft(modeButtonWidth));
    modeRow2.removeFromLeft(modeButtonGap);
    dirtyButton.setBounds(modeRow2);

    // Tab bar (28px) — store area for mouseDown hit testing
    tabBarArea = bounds.removeFromTop(28);
    tabBarArea.reduce(8, 2);

    // --- Knob layout helpers ---
    int knobSize = 50;
    int labelHeight = 14;

    auto layoutKnobRow = [&](juce::Rectangle<int>& parentBounds, int sectionHeight,
                             juce::Slider* sliders[], juce::Label* labels[], int count,
                             int preDelaySyncIdx = -1)
    {
        parentBounds.removeFromTop(5);
        auto section = parentBounds.removeFromTop(sectionHeight);
        section.reduce(12, 4);
        section.removeFromTop(14); // Section label space

        int cellWidth = section.getWidth() / 4;

        for (int i = 0; i < count; ++i)
        {
            auto cell = section.withX(section.getX() + i * cellWidth).withWidth(cellWidth);
            labels[i]->setBounds(cell.removeFromTop(labelHeight));

            if (i == preDelaySyncIdx)
            {
                auto syncArea = cell.removeFromBottom(22).reduced(2, 0);
                int syncBtnW = syncArea.getWidth() / 3;
                preDelaySyncButton.setBounds(syncArea.removeFromLeft(syncBtnW));
                syncArea.removeFromLeft(2);
                preDelayNoteBox.setBounds(syncArea);
            }

            sliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize + 16));
        }
    };

    // Layout for a row with a ComboBox in the first cell + sliders for the rest
    auto layoutComboRow = [&](juce::Rectangle<int>& parentBounds, int sectionHeight,
                              juce::ComboBox& combo, juce::Label& comboLabel,
                              juce::Slider* sliders[], juce::Label* labels[], int sliderCount)
    {
        parentBounds.removeFromTop(5);
        auto section = parentBounds.removeFromTop(sectionHeight);
        section.reduce(12, 4);
        section.removeFromTop(14); // Section label space

        int cellWidth = section.getWidth() / 4;

        // Cell 0: ComboBox
        auto comboCell = section.withWidth(cellWidth);
        comboLabel.setBounds(comboCell.removeFromTop(labelHeight));
        combo.setBounds(comboCell.withSizeKeepingCentre(cellWidth - 10, 24));

        // Cells 1+: Sliders
        for (int i = 0; i < sliderCount; ++i)
        {
            auto cell = section.withX(section.getX() + (i + 1) * cellWidth).withWidth(cellWidth);
            labels[i]->setBounds(cell.removeFromTop(labelHeight));
            sliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize + 16));
        }
    };

    // === Tab-specific layout ===
    if (currentTab == 0)
    {
        // Tab 0: MAIN — 3 rows of 4
        juce::Slider* row1Sliders[] = { &sizeSlider, &preDelaySlider, &shapeSlider, &spreadSlider };
        juce::Label* row1Labels[] = { &sizeLabel, &preDelayLabel, &shapeLabel, &spreadLabel };
        layoutKnobRow(bounds, 110, row1Sliders, row1Labels, 4, 1);

        juce::Slider* row2Sliders[] = { &dampingSlider, &bassBoostSlider, &hfDecaySlider, &diffusionSlider };
        juce::Label* row2Labels[] = { &dampingLabel, &bassBoostLabel, &hfDecayLabel, &diffusionLabel };
        layoutKnobRow(bounds, 110, row2Sliders, row2Labels, 4);

        juce::Slider* row3Sliders[] = { &widthSlider, &mixSlider, &lowCutSlider, &highCutSlider };
        juce::Label* row3Labels[] = { &widthLabel, &mixLabel, &lowCutLabel, &highCutLabel };
        layoutKnobRow(bounds, 110, row3Sliders, row3Labels, 4);
    }
    else if (currentTab == 1)
    {
        // Tab 1: DECAY — 3 rows of 4
        juce::Slider* row1Sliders[] = { &roomSizeSlider, &earlyDiffSlider, &erLateBalSlider, &erBassCutSlider };
        juce::Label* row1Labels[] = { &roomSizeLabel, &earlyDiffLabel, &erLateBalLabel, &erBassCutLabel };
        layoutKnobRow(bounds, 110, row1Sliders, row1Labels, 4);

        juce::Slider* row2Sliders[] = { &bassFreqSlider, &midDecaySlider, &highFreqSlider, &trebleRatioSlider };
        juce::Label* row2Labels[] = { &bassFreqLabel, &midDecayLabel, &highFreqLabel, &trebleRatioLabel };
        layoutKnobRow(bounds, 110, row2Sliders, row2Labels, 4);

        juce::Slider* row3Sliders[] = { &lowMidFreqSlider, &lowMidDecaySlider, &modRateSlider, &modDepthSlider };
        juce::Label* row3Labels[] = { &lowMidFreqLabel, &lowMidDecayLabel, &modRateLabel, &modDepthLabel };
        layoutKnobRow(bounds, 110, row3Sliders, row3Labels, 4);
    }
    else if (currentTab == 2)
    {
        // Tab 2: EFFECTS — Row 1: ComboBox + 3 sliders, Row 2: 4 sliders, Row 3: 4 sliders
        juce::Slider* envSliders[] = { &envDepthSlider, &envHoldSlider, &envReleaseSlider };
        juce::Label* envLabels[] = { &envDepthLabel, &envHoldLabel, &envReleaseLabel };
        layoutComboRow(bounds, 110, envModeBox, envModeLabel, envSliders, envLabels, 3);

        juce::Slider* echoSliders[] = { &echoDelaySlider, &echoFeedbackSlider, &echoPingPongSlider, &resonanceSlider };
        juce::Label* echoLabels[] = { &echoDelayLabel, &echoFeedbackLabel, &echoPingPongLabel, &resonanceLabel };
        layoutKnobRow(bounds, 110, echoSliders, echoLabels, 4);

        juce::Slider* dynSliders[] = { &dynAmountSlider, &dynSpeedSlider, &stereoCouplingSlider, &stereoInvertSlider };
        juce::Label* dynLabels[] = { &dynAmountLabel, &dynSpeedLabel, &stereoCouplingLabel, &stereoInvertLabel };
        layoutKnobRow(bounds, 110, dynSliders, dynLabels, 4);
    }
    else if (currentTab == 3)
    {
        // Tab 3: OUTPUT EQ — 2 rows of 3
        juce::Slider* eq1Sliders[] = { &outEQ1FreqSlider, &outEQ1GainSlider, &outEQ1QSlider };
        juce::Label* eq1Labels[] = { &outEQ1FreqLabel, &outEQ1GainLabel, &outEQ1QLabel };
        layoutKnobRow(bounds, 110, eq1Sliders, eq1Labels, 3);

        juce::Slider* eq2Sliders[] = { &outEQ2FreqSlider, &outEQ2GainSlider, &outEQ2QSlider };
        juce::Label* eq2Labels[] = { &outEQ2FreqLabel, &outEQ2GainLabel, &outEQ2QLabel };
        layoutKnobRow(bounds, 110, eq2Sliders, eq2Labels, 3);
    }

    // LED meter (right strip, spans from modes to bottom)
    outputMeter.setBounds(meterStrip.withTrimmedTop(30).withTrimmedBottom(16).reduced(4, 0));

    // Overlays (full size)
    if (supportersOverlay)
        supportersOverlay->setBounds(getLocalBounds());
    if (presetBrowser)
        presetBrowser->setBounds(getLocalBounds());
}

void Velvet90Editor::timerCallback()
{
    // Update buttons in case parameters changed externally
    updateModeButtons();

    // Update preset display (includes RT60 in LCD)
    updatePresetDisplay();

    // Update RT60 in LCD right side
    float rt60 = audioProcessor.getRT60Display();
    if (rt60 < 10.0f)
        lcdDisplay.setLine1Right(juce::String(rt60, 1) + "s");
    else
        lcdDisplay.setLine1Right(juce::String(static_cast<int>(rt60)) + "s");

    // Update LED meter
    float peakL = audioProcessor.getOutputLevelL();
    float peakR = audioProcessor.getOutputLevelR();
    float dbL = juce::Decibels::gainToDecibels(peakL, -60.0f);
    float dbR = juce::Decibels::gainToDecibels(peakR, -60.0f);
    outputMeter.setStereoLevels(dbL, dbR);
    outputMeter.repaint();
}

void Velvet90Editor::mouseDown(const juce::MouseEvent& event)
{
    if (titleClickArea.contains(event.getPosition()))
    {
        showSupportersPanel();
        return;
    }

    // Tab bar click handling
    if (tabBarArea.contains(event.getPosition()))
    {
        int tabWidth = tabBarArea.getWidth() / 4;
        int clickedTab = (event.getPosition().x - tabBarArea.getX()) / tabWidth;
        clickedTab = juce::jlimit(0, 3, clickedTab);
        if (clickedTab != currentTab)
            switchTab(clickedTab);
        return;
    }

    juce::AudioProcessorEditor::mouseDown(event);
}

void Velvet90Editor::showSupportersPanel()
{
    if (supportersOverlay == nullptr)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>("Velvet 90", "1.0.0");
        supportersOverlay->onDismiss = [this]() {
            supportersOverlay.reset();
        };
        addAndMakeVisible(supportersOverlay.get());
        supportersOverlay->setBounds(getLocalBounds());
    }
}

void Velvet90Editor::showPresetBrowser()
{
    if (presetBrowser == nullptr)
    {
        presetBrowser = std::make_unique<PresetBrowserOverlay>(audioProcessor);
        presetBrowser->onDismiss = [this]() {
            presetBrowser.reset();
            updatePresetDisplay();
            updateModeButtons();
        };
        addAndMakeVisible(presetBrowser.get());
        presetBrowser->setBounds(getLocalBounds());
    }
}

void Velvet90Editor::navigatePreset(int delta)
{
    int numPrograms = audioProcessor.getNumPrograms();
    int current = audioProcessor.getCurrentProgram();
    int next = (current + delta + numPrograms) % numPrograms;
    audioProcessor.setCurrentProgram(next);
    updatePresetDisplay();
    updateModeButtons();
}

void Velvet90Editor::updatePresetDisplay()
{
    int prog = audioProcessor.getCurrentProgram();
    auto presets = Velvet90Presets::getFactoryPresets();

    static const juce::StringArray modeNames = {
        "Plate", "Room", "Hall", "Chamber", "Cathedral", "Ambience",
        "Bright Hall", "Chorus Space", "Random Space", "Dirty Hall"
    };

    if (prog == 0)
    {
        lcdDisplay.setLine1("");
        lcdDisplay.setLine2("Init");
    }
    else if (prog > 0 && prog <= static_cast<int>(presets.size()))
    {
        const auto& preset = presets[static_cast<size_t>(prog - 1)];
        juce::String modeName = (preset.mode >= 0 && preset.mode < modeNames.size())
            ? modeNames[preset.mode] : "";

        lcdDisplay.setLine1(preset.category + ":  " + modeName);
        lcdDisplay.setLine2(juce::String(prog).paddedLeft('0', 2) + "  " + preset.name);
    }
}

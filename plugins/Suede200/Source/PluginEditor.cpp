/*
  ==============================================================================

    PluginEditor.cpp
    Suede 200 — Vintage Digital Reverberator

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Suede200LookAndFeel
//==============================================================================
Suede200LookAndFeel::Suede200LookAndFeel()
{
    // Dark charcoal theme matching the original Model 200 chassis
    setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1c1c1c));
    setColour(juce::Slider::thumbColourId, juce::Colour(0xff4a8a4a));
    setColour(juce::Slider::trackColourId, juce::Colour(0xff2a2a2a));
    setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
}

void Suede200LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
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

    // Shadow
    g.setColour(juce::Colour(0x40000000));
    g.fillEllipse(rx + 2, ry + 2, rw, rw);

    // Knob body — dark metal with subtle gradient
    juce::ColourGradient bodyGradient(
        juce::Colour(0xff404040), centreX - radius * 0.7f, centreY - radius * 0.7f,
        juce::Colour(0xff1a1a1a), centreX + radius * 0.7f, centreY + radius * 0.7f,
        true);
    g.setGradientFill(bodyGradient);
    g.fillEllipse(rx, ry, rw, rw);

    // Outer ring — dark chrome
    g.setColour(juce::Colour(0xff5a5a5a));
    g.drawEllipse(rx, ry, rw, rw, 2.0f);

    // Arc track (background)
    juce::Path arcBg;
    arcBg.addCentredArc(centreX, centreY, radius - 4, radius - 4,
                        0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff2a2a2a));
    g.strokePath(arcBg, juce::PathStrokeType(3.0f));

    // Arc track (value) — green LED color (Suede 200 aesthetic)
    if (sliderPos > 0.0f)
    {
        juce::Path arcValue;
        arcValue.addCentredArc(centreX, centreY, radius - 4, radius - 4,
                              0.0f, rotaryStartAngle, angle, true);
        g.setColour(juce::Colour(0xff4a8a4a));
        g.strokePath(arcValue, juce::PathStrokeType(3.0f));
    }

    // Pointer line
    juce::Path pointer;
    auto pointerLength = radius * 0.6f;
    auto pointerThickness = 3.0f;
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -radius + 8,
                                pointerThickness, pointerLength, 1.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    g.setColour(juce::Colour(0xffe0e0e0));
    g.fillPath(pointer);

    juce::ignoreUnused(slider);
}

void Suede200LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2);
    auto isOn = button.getToggleState();

    if (isOn)
    {
        // Active: green LED glow
        g.setColour(juce::Colour(0xff4a8a4a).withAlpha(0.2f));
        g.fillRoundedRectangle(bounds.expanded(2), 6.0f);

        juce::ColourGradient gradient(
            juce::Colour(0xff3a7a3a), bounds.getCentreX(), bounds.getY(),
            juce::Colour(0xff2a5a2a), bounds.getCentreX(), bounds.getBottom(),
            false);
        g.setGradientFill(gradient);
    }
    else
    {
        juce::ColourGradient gradient(
            juce::Colour(0xff383838), bounds.getCentreX(), bounds.getY(),
            juce::Colour(0xff282828), bounds.getCentreX(), bounds.getBottom(),
            false);
        g.setGradientFill(gradient);
    }
    g.fillRoundedRectangle(bounds, 5.0f);

    // Border
    g.setColour(isOn ? juce::Colour(0xff4a8a4a) : juce::Colour(0xff484848));
    g.drawRoundedRectangle(bounds, 5.0f, 1.5f);

    // Hover highlight
    if (shouldDrawButtonAsHighlighted && !isOn)
    {
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(bounds, 5.0f);
    }

    // Text
    g.setColour(isOn ? juce::Colour(0xffffffff) : juce::Colour(0xff909090));
    g.setFont(juce::Font(juce::FontOptions(11.0f)).withStyle(juce::Font::bold));
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred);

    juce::ignoreUnused(shouldDrawButtonAsDown);
}

//==============================================================================
// ThreeWaySelector
//==============================================================================
ThreeWaySelector::ThreeWaySelector(const juce::String& label,
                                    const juce::StringArray& options)
    : labelText(label), optionLabels(options)
{
}

void ThreeWaySelector::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Label at top
    auto labelArea = bounds.removeFromTop(14);
    g.setFont(juce::Font(juce::FontOptions(9.0f)).withStyle(juce::Font::bold));
    g.setColour(juce::Colour(0xff909090));
    g.drawText(labelText, labelArea, juce::Justification::centred);

    // Three option buttons
    int optionHeight = bounds.getHeight() / 3;
    for (int i = 0; i < 3 && i < optionLabels.size(); ++i)
    {
        auto optionArea = bounds.removeFromTop(optionHeight).reduced(2, 1);
        bool isSelected = (i == selectedIndex);

        if (isSelected)
        {
            // Green LED indicator
            g.setColour(juce::Colour(0xff3a7a3a));
            g.fillRoundedRectangle(optionArea.toFloat(), 3.0f);
        }
        else
        {
            g.setColour(juce::Colour(0xff2a2a2a));
            g.fillRoundedRectangle(optionArea.toFloat(), 3.0f);
        }

        g.setColour(isSelected ? juce::Colour(0xff4a8a4a) : juce::Colour(0xff404040));
        g.drawRoundedRectangle(optionArea.toFloat(), 3.0f, 1.0f);

        // LED dot
        auto ledDot = optionArea.removeFromLeft(12).withSizeKeepingCentre(6, 6);
        if (isSelected)
        {
            g.setColour(juce::Colour(0xff80ff80));
            g.fillEllipse(ledDot.toFloat());
            g.setColour(juce::Colour(0x4080ff80));
            g.fillEllipse(ledDot.toFloat().expanded(2));
        }
        else
        {
            g.setColour(juce::Colour(0xff303030));
            g.fillEllipse(ledDot.toFloat());
        }

        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.setColour(isSelected ? juce::Colour(0xffffffff) : juce::Colour(0xff808080));
        g.drawText(optionLabels[i], optionArea, juce::Justification::centredLeft);
    }
}

void ThreeWaySelector::mouseDown(const juce::MouseEvent& event)
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(14); // skip label

    int optionHeight = bounds.getHeight() / 3;
    int clickedIndex = (event.getPosition().y - bounds.getY()) / optionHeight;
    clickedIndex = juce::jlimit(0, 2, clickedIndex);

    if (clickedIndex != selectedIndex)
    {
        selectedIndex = clickedIndex;
        repaint();
        if (onChange) onChange(selectedIndex);
    }
}

void ThreeWaySelector::setSelectedIndex(int index)
{
    selectedIndex = juce::jlimit(0, 2, index);
    repaint();
}

//==============================================================================
// Suede200PresetBrowser
//==============================================================================
Suede200PresetBrowser::Suede200PresetBrowser(Suede200Processor& p)
    : processor(p)
{
    auto presets = Suede200Presets::getFactoryPresets();
    for (const auto& preset : presets)
    {
        if (std::find(categoryOrder.begin(), categoryOrder.end(), preset.category) == categoryOrder.end())
            categoryOrder.push_back(preset.category);
    }
    if (!categoryOrder.empty())
        selectedCategory = categoryOrder[0];
}

void Suede200PresetBrowser::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xd0101010));

    auto panel = getLocalBounds().reduced(20, 35);
    g.setColour(juce::Colour(0xff1e1e1e));
    g.fillRoundedRectangle(panel.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff4a8a4a));
    g.drawRoundedRectangle(panel.toFloat(), 8.0f, 1.5f);

    auto header = panel.removeFromTop(30);
    g.setFont(juce::Font(juce::FontOptions(14.0f)).withStyle(juce::Font::bold));
    g.setColour(juce::Colour(0xff4a8a4a));
    g.drawText("PRESETS", header, juce::Justification::centred);

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
            g.setColour(juce::Colour(0xff2a4a2a));
            g.fillRoundedRectangle(tab.reduced(1).toFloat(), 4.0f);
        }

        g.setColour(isSelected ? juce::Colour(0xffffffff) : juce::Colour(0xff808080));
        g.drawText(categoryOrder[i], tab, juce::Justification::centred);
    }

    panel.removeFromTop(10);

    // Init entry
    int currentProg = processor.getCurrentProgram();
    auto initRow = panel.removeFromTop(22);
    initRow.reduce(10, 0);
    if (currentProg == 0)
    {
        g.setColour(juce::Colour(0xff2a3a2a));
        g.fillRoundedRectangle(initRow.toFloat(), 3.0f);
    }
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(currentProg == 0 ? juce::Colour(0xff4a8a4a) : juce::Colour(0xffb0b0b0));
    g.drawText("Init", initRow.reduced(8, 0), juce::Justification::centredLeft);

    panel.removeFromTop(3);

    auto presets = Suede200Presets::getFactoryPresets();
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
            g.setColour(juce::Colour(0xff2a3a2a));
            g.fillRoundedRectangle(row.toFloat(), 3.0f);
        }

        g.setColour(currentProg == progIdx ? juce::Colour(0xff4a8a4a) : juce::Colour(0xffc0c0c0));
        g.drawText(presets[i].name, row.reduced(8, 0), juce::Justification::centredLeft);
    }
}

void Suede200PresetBrowser::mouseDown(const juce::MouseEvent& event)
{
    auto panel = getLocalBounds().reduced(20, 35);

    if (!panel.contains(event.getPosition()))
    {
        if (onDismiss) onDismiss();
        return;
    }

    panel.removeFromTop(30);
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

    panel.removeFromTop(10);

    auto initRow = panel.removeFromTop(22);
    initRow.reduce(10, 0);
    if (initRow.contains(event.getPosition()))
    {
        processor.setCurrentProgram(0);
        if (onDismiss) onDismiss();
        return;
    }

    panel.removeFromTop(3);

    auto presets = Suede200Presets::getFactoryPresets();
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
// Suede200Editor
//==============================================================================
Suede200Editor::Suede200Editor(Suede200Processor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      diffusionSelector("DIFFUSION", { "Lo", "Med", "Hi" }),
      rtLowSelector("RT LOW 100Hz", { "X0.5", "X1.0", "X1.5" }),
      rtHighSelector("RT HIGH 10kHz", { "X0.25", "X0.5", "X1.0" }),
      rolloffSelector("ROLLOFF", { "3 kHz", "7 kHz", "10 kHz" })
{
    setLookAndFeel(&lookAndFeel);

    // Program buttons
    auto setupProgButton = [this](juce::ToggleButton& btn, const juce::String& text, int prog) {
        btn.setButtonText(text);
        btn.setRadioGroupId(1);
        btn.setClickingTogglesState(true);
        btn.onClick = [this, prog]() { programButtonClicked(prog); };
        addAndMakeVisible(btn);
    };

    setupProgButton(concertHallButton, "CONCERT HALL", 0);
    setupProgButton(plateButton, "PLATE", 1);
    setupProgButton(chamberButton, "CHAMBER", 2);
    setupProgButton(richPlateButton, "RICH PLATE", 3);
    setupProgButton(richSplitsButton, "RICH SPLITS", 4);
    setupProgButton(inverseRoomsButton, "INVERSE", 5);

    // Main knobs
    setupSlider(predelaySlider, predelayLabel, "PRE-DELAY");
    predelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "predelay", predelaySlider);

    setupSlider(reverbTimeSlider, reverbTimeLabel, "REVERB TIME");
    reverbTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "reverbtime", reverbTimeSlider);

    setupSlider(sizeSlider, sizeLabel, "SIZE");
    sizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "size", sizeSlider);

    setupSlider(mixSlider, mixLabel, "MIX");
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mix", mixSlider);

    // Pre-Echoes toggle
    preEchoesButton.setButtonText("PRE-ECHOES");
    preEchoesButton.setClickingTogglesState(true);
    addAndMakeVisible(preEchoesButton);
    preEchoesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "preechoes", preEchoesButton);

    // Three-way selectors — sync with APVTS
    diffusionSelector.onChange = [this](int idx) {
        audioProcessor.getAPVTS().getParameterAsValue("diffusion").setValue(idx);
    };
    addAndMakeVisible(diffusionSelector);

    rtLowSelector.onChange = [this](int idx) {
        audioProcessor.getAPVTS().getParameterAsValue("rtlow").setValue(idx);
    };
    addAndMakeVisible(rtLowSelector);

    rtHighSelector.onChange = [this](int idx) {
        audioProcessor.getAPVTS().getParameterAsValue("rthigh").setValue(idx);
    };
    addAndMakeVisible(rtHighSelector);

    rolloffSelector.onChange = [this](int idx) {
        audioProcessor.getAPVTS().getParameterAsValue("rolloff").setValue(idx);
    };
    addAndMakeVisible(rolloffSelector);

    // LED output meter
    outputMeter.setStereoMode(true);
    outputMeter.setRefreshRate(30.0f);
    addAndMakeVisible(outputMeter);

    // Preset navigation
    prevPresetButton.setButtonText("<");
    prevPresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0a0a0a));
    prevPresetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff4a8a4a));
    prevPresetButton.onClick = [this]() { navigatePreset(-1); };
    addAndMakeVisible(prevPresetButton);

    nextPresetButton.setButtonText(">");
    nextPresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0a0a0a));
    nextPresetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff4a8a4a));
    nextPresetButton.onClick = [this]() { navigatePreset(1); };
    addAndMakeVisible(nextPresetButton);

    presetNameLabel.setJustificationType(juce::Justification::centred);
    presetNameLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4a8a4a));
    presetNameLabel.setFont(juce::Font(juce::FontOptions(12.0f)).withStyle(juce::Font::bold));
    presetNameLabel.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(presetNameLabel);

    // Tooltips
    predelaySlider.setTooltip(DuskTooltips::withAllHints("Delay before reverb onset (ms)"));
    reverbTimeSlider.setTooltip(DuskTooltips::withAllHints("Reverb decay time — RT60 at 1kHz"));
    sizeSlider.setTooltip(DuskTooltips::withAllHints("Room size in meters"));
    mixSlider.setTooltip(DuskTooltips::withAllHints("Dry/wet output balance"));
    preEchoesButton.setTooltip("Stage reflection emulation (varies per program)");

    updateProgramButtons();
    updateDiscreteParams();
    updatePresetDisplay();

    startTimerHz(30);

    resizeHelper.initialize(this, &audioProcessor, 650, 420, 520, 336, 780, 504, false);
    setSize(resizeHelper.getStoredWidth(), resizeHelper.getStoredHeight());
}

Suede200Editor::~Suede200Editor()
{
    resizeHelper.saveSize();
    stopTimer();
    setLookAndFeel(nullptr);
}

void Suede200Editor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 65, 18);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe0e0e0));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff2a2a2a));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff3a3a3a));
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    label.setFont(juce::Font(juce::FontOptions(10.0f)).withStyle(juce::Font::bold));
    addAndMakeVisible(label);
}

void Suede200Editor::updateProgramButtons()
{
    auto* param = audioProcessor.getAPVTS().getRawParameterValue("program");
    if (!param) return;

    int prog = static_cast<int>(param->load());
    concertHallButton.setToggleState(prog == 0, juce::dontSendNotification);
    plateButton.setToggleState(prog == 1, juce::dontSendNotification);
    chamberButton.setToggleState(prog == 2, juce::dontSendNotification);
    richPlateButton.setToggleState(prog == 3, juce::dontSendNotification);
    richSplitsButton.setToggleState(prog == 4, juce::dontSendNotification);
    inverseRoomsButton.setToggleState(prog == 5, juce::dontSendNotification);
}

void Suede200Editor::programButtonClicked(int program)
{
    audioProcessor.getAPVTS().getParameterAsValue("program").setValue(program);
    updateProgramButtons();
}

void Suede200Editor::updateDiscreteParams()
{
    auto& apvts = audioProcessor.getAPVTS();

    diffusionSelector.setSelectedIndex(static_cast<int>(apvts.getRawParameterValue("diffusion")->load()));
    rtLowSelector.setSelectedIndex(static_cast<int>(apvts.getRawParameterValue("rtlow")->load()));
    rtHighSelector.setSelectedIndex(static_cast<int>(apvts.getRawParameterValue("rthigh")->load()));
    rolloffSelector.setSelectedIndex(static_cast<int>(apvts.getRawParameterValue("rolloff")->load()));
}

void Suede200Editor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1c1c));

    auto bounds = getLocalBounds();

    // Header area
    auto headerArea = bounds.removeFromTop(50);
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(headerArea);

    // Title
    auto titleRow = headerArea.withHeight(28);
    titleClickArea = titleRow.withWidth(130).withX(titleRow.getX() + 10);

    g.setFont(juce::Font(juce::FontOptions(20.0f)).withStyle(juce::Font::bold));
    g.setColour(juce::Colour(0xff4a8a4a));
    g.drawText("Suede 200", titleRow.reduced(12, 0), juce::Justification::centredLeft);

    // Subtitle
    g.setFont(juce::Font(juce::FontOptions(9.0f)).withStyle(juce::Font::italic));
    g.setColour(juce::Colour(0xff606060));
    g.drawText("Vintage Digital Reverberator", titleRow.reduced(12, 0),
               juce::Justification::centredRight);

    // Reserve right for meter
    bounds.removeFromRight(30);

    // Program section background
    auto progArea = bounds.removeFromTop(30);
    progArea.reduce(8, 2);
    g.setColour(juce::Colour(0xff232323));
    g.fillRoundedRectangle(progArea.toFloat(), 5.0f);

    // Section label: KNOBS
    bounds.removeFromTop(4);
    auto knobSection = bounds.removeFromTop(120);
    knobSection.reduce(8, 0);
    g.setColour(juce::Colour(0xff262626));
    g.fillRoundedRectangle(knobSection.toFloat(), 5.0f);

    // Section label: CONTROLS
    bounds.removeFromTop(4);
    auto controlSection = bounds.removeFromTop(120);
    controlSection.reduce(8, 0);
    g.setColour(juce::Colour(0xff262626));
    g.fillRoundedRectangle(controlSection.toFloat(), 5.0f);

    // Section header labels
    g.setFont(juce::Font(juce::FontOptions(9.0f)).withStyle(juce::Font::bold));
    g.setColour(juce::Colour(0xff4a8a4a));
    g.drawText("REVERB", knobSection.removeFromTop(14).reduced(10, 0), juce::Justification::centredLeft);
    g.drawText("CONTOUR", controlSection.removeFromTop(14).reduced(10, 0), juce::Justification::centredLeft);

    // Footer
    g.setFont(juce::Font(juce::FontOptions(9.0f)).withStyle(juce::Font::italic));
    g.setColour(juce::Colour(0xff505050));
    g.drawText("Dusk Audio", getLocalBounds().removeFromBottom(14), juce::Justification::centred);
}

void Suede200Editor::resized()
{
    resizeHelper.updateResizer();

    auto bounds = getLocalBounds();

    // Header (50px)
    auto headerArea = bounds.removeFromTop(50);

    // Preset navigation in lower header
    auto presetRow = headerArea.withTop(28).withHeight(20).reduced(160, 0);
    prevPresetButton.setBounds(presetRow.removeFromLeft(22));
    nextPresetButton.setBounds(presetRow.removeFromRight(22));
    presetNameLabel.setBounds(presetRow.reduced(3, 0));

    // Meter strip
    auto meterStrip = bounds.removeFromRight(30);

    // Program buttons (1 row of 6)
    auto progSection = bounds.removeFromTop(30);
    progSection.reduce(12, 2);
    int progButtonWidth = (progSection.getWidth() - 15) / 6; // 5 gaps of 3px
    int progGap = 3;

    concertHallButton.setBounds(progSection.removeFromLeft(progButtonWidth));
    progSection.removeFromLeft(progGap);
    plateButton.setBounds(progSection.removeFromLeft(progButtonWidth));
    progSection.removeFromLeft(progGap);
    chamberButton.setBounds(progSection.removeFromLeft(progButtonWidth));
    progSection.removeFromLeft(progGap);
    richPlateButton.setBounds(progSection.removeFromLeft(progButtonWidth));
    progSection.removeFromLeft(progGap);
    richSplitsButton.setBounds(progSection.removeFromLeft(progButtonWidth));
    progSection.removeFromLeft(progGap);
    inverseRoomsButton.setBounds(progSection);

    // Knob section (4 knobs)
    bounds.removeFromTop(4);
    auto knobArea = bounds.removeFromTop(120);
    knobArea.reduce(12, 4);
    knobArea.removeFromTop(14); // section label

    int knobWidth = knobArea.getWidth() / 4;
    int knobSize = 55;

    auto layoutKnob = [&](juce::Slider& slider, juce::Label& label, int index) {
        auto cell = knobArea.withX(knobArea.getX() + index * knobWidth).withWidth(knobWidth);
        label.setBounds(cell.removeFromTop(14));
        slider.setBounds(cell.withSizeKeepingCentre(knobSize, knobSize + 18));
    };

    layoutKnob(predelaySlider, predelayLabel, 0);
    layoutKnob(reverbTimeSlider, reverbTimeLabel, 1);
    layoutKnob(sizeSlider, sizeLabel, 2);
    layoutKnob(mixSlider, mixLabel, 3);

    // Control section (4 selectors + Pre-Echoes toggle)
    bounds.removeFromTop(4);
    auto controlArea = bounds.removeFromTop(120);
    controlArea.reduce(12, 4);
    controlArea.removeFromTop(14); // section label

    int selectorWidth = controlArea.getWidth() / 5;

    diffusionSelector.setBounds(controlArea.withX(controlArea.getX()).withWidth(selectorWidth).reduced(4, 0));
    rtLowSelector.setBounds(controlArea.withX(controlArea.getX() + selectorWidth).withWidth(selectorWidth).reduced(4, 0));
    rtHighSelector.setBounds(controlArea.withX(controlArea.getX() + selectorWidth * 2).withWidth(selectorWidth).reduced(4, 0));
    rolloffSelector.setBounds(controlArea.withX(controlArea.getX() + selectorWidth * 3).withWidth(selectorWidth).reduced(4, 0));

    // Pre-Echoes in the 5th column
    auto echoCell = controlArea.withX(controlArea.getX() + selectorWidth * 4).withWidth(selectorWidth).reduced(4, 0);
    preEchoesButton.setBounds(echoCell.withSizeKeepingCentre(selectorWidth - 10, 28));

    // LED meter
    outputMeter.setBounds(meterStrip.withTrimmedTop(30).withTrimmedBottom(16).reduced(4, 0));

    // Overlays
    if (supportersOverlay)
        supportersOverlay->setBounds(getLocalBounds());
    if (presetBrowser)
        presetBrowser->setBounds(getLocalBounds());
}

void Suede200Editor::timerCallback()
{
    updateProgramButtons();
    updateDiscreteParams();
    updatePresetDisplay();

    // Update LED meter
    float peakL = audioProcessor.getOutputLevelL();
    float peakR = audioProcessor.getOutputLevelR();
    float dbL = juce::Decibels::gainToDecibels(peakL, -60.0f);
    float dbR = juce::Decibels::gainToDecibels(peakR, -60.0f);
    outputMeter.setStereoLevels(dbL, dbR);
    outputMeter.repaint();
}

void Suede200Editor::mouseDown(const juce::MouseEvent& event)
{
    if (titleClickArea.contains(event.getPosition()))
    {
        showSupportersPanel();
        return;
    }

    // Click on preset name label area → show browser
    if (presetNameLabel.getBounds().contains(event.getPosition()))
    {
        showPresetBrowser();
        return;
    }

    juce::AudioProcessorEditor::mouseDown(event);
}

void Suede200Editor::showSupportersPanel()
{
    if (supportersOverlay == nullptr)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>("Suede 200", "0.1.0");
        supportersOverlay->onDismiss = [this]() { supportersOverlay.reset(); };
        addAndMakeVisible(supportersOverlay.get());
        supportersOverlay->setBounds(getLocalBounds());
    }
}

void Suede200Editor::showPresetBrowser()
{
    if (presetBrowser == nullptr)
    {
        presetBrowser = std::make_unique<Suede200PresetBrowser>(audioProcessor);
        presetBrowser->onDismiss = [this]() {
            presetBrowser.reset();
            updatePresetDisplay();
            updateProgramButtons();
            updateDiscreteParams();
        };
        addAndMakeVisible(presetBrowser.get());
        presetBrowser->setBounds(getLocalBounds());
    }
}

void Suede200Editor::navigatePreset(int delta)
{
    int numPrograms = audioProcessor.getNumPrograms();
    int current = audioProcessor.getCurrentProgram();
    int next = (current + delta + numPrograms) % numPrograms;
    audioProcessor.setCurrentProgram(next);
    updatePresetDisplay();
    updateProgramButtons();
    updateDiscreteParams();
}

void Suede200Editor::updatePresetDisplay()
{
    int prog = audioProcessor.getCurrentProgram();
    auto presets = Suede200Presets::getFactoryPresets();

    if (prog == 0)
    {
        presetNameLabel.setText("Init", juce::dontSendNotification);
    }
    else if (prog > 0 && prog <= static_cast<int>(presets.size()))
    {
        presetNameLabel.setText(presets[static_cast<size_t>(prog - 1)].name,
                               juce::dontSendNotification);
    }
}

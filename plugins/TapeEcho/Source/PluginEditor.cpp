/*
  ==============================================================================

    RE-201 Space Echo - Plugin Editor Implementation
    UAD Galaxy-style 3-layer hardware emulation UI
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

TapeEchoEditor::TapeEchoEditor(TapeEchoProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      stereoSwitch("STEREO")
{
    // UAD Galaxy-style hardware proportions (wide format)
    setSize(950, 380);
    setResizable(true, true);
    setResizeLimits(760, 304, 1330, 532);

    // Apply look and feel globally
    setLookAndFeel(&lookAndFeel);

    setupControls();
    setupLabels();

    addAndMakeVisible(vuMeter);
    addAndMakeVisible(modeSelector);
    addAndMakeVisible(presetSelector);
    addAndMakeVisible(stereoSwitch);

    // Setup preset selector
    presetSelector.addItem("User", 1);
    for (size_t i = 0; i < audioProcessor.getFactoryPresets().size(); ++i)
    {
        presetSelector.addItem(audioProcessor.getFactoryPresets()[i].name, static_cast<int>(i + 2));
    }
    presetSelector.setSelectedId(1);
    presetSelector.addListener(this);

    // Stereo switch callback
    stereoSwitch.onStateChange = [this](bool isOn) {
        auto* param = audioProcessor.apvts.getParameter(TapeEchoProcessor::PARAM_STEREO_MODE);
        if (param)
            param->setValueNotifyingHost(isOn ? 1.0f : 0.0f);
    };

    // Mode selector callback
    modeSelector.onModeChanged = [this](int mode) {
        auto* param = audioProcessor.apvts.getParameter(TapeEchoProcessor::PARAM_MODE);
        if (param)
            param->setValueNotifyingHost(mode / 11.0f);
    };

    // Create parameter attachments
    repeatRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_REPEAT_RATE, repeatRateKnob);

    intensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_INTENSITY, intensityKnob);

    echoVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_ECHO_VOLUME, echoVolumeKnob);

    reverbVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_REVERB_VOLUME, reverbVolumeKnob);

    bassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_BASS, bassKnob);

    trebleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_TREBLE, trebleKnob);

    inputVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_INPUT_VOLUME, inputVolumeKnob);

    wowFlutterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_WOW_FLUTTER, wowFlutterKnob);

    tapeAgeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_TAPE_AGE, tapeAgeKnob);

    motorTorqueAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_MOTOR_TORQUE, motorTorqueKnob);

    startTimerHz(30);
}

TapeEchoEditor::~TapeEchoEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void TapeEchoEditor::setupControls()
{
    auto setupKnob = [this](juce::Slider& knob, const juce::String& suffix = "") {
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob.setLookAndFeel(&lookAndFeel);
        if (suffix.isNotEmpty())
            knob.setTextValueSuffix(suffix);
        addAndMakeVisible(knob);
    };

    // Main knobs
    setupKnob(repeatRateKnob, " ms");
    setupKnob(intensityKnob, " %");
    setupKnob(echoVolumeKnob, " %");
    setupKnob(reverbVolumeKnob, " %");
    setupKnob(bassKnob, " dB");
    setupKnob(trebleKnob, " dB");
    setupKnob(inputVolumeKnob, " %");

    // Extended controls (smaller)
    setupKnob(wowFlutterKnob);
    setupKnob(tapeAgeKnob);
    setupKnob(motorTorqueKnob);
}

void TapeEchoEditor::setupLabels()
{
    auto setupLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        label.setColour(juce::Label::textColourId, RE201Colours::textWhite);
        label.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(label);
    };

    setupLabel(repeatRateLabel, "ECHO RATE");
    setupLabel(intensityLabel, "FEEDBACK");
    setupLabel(echoVolumeLabel, "ECHO VOL");
    setupLabel(reverbVolumeLabel, "REVERB VOL");
    setupLabel(bassLabel, "BASS");
    setupLabel(trebleLabel, "TREBLE");
    setupLabel(inputVolumeLabel, "INPUT VOL");
    setupLabel(wowFlutterLabel, "WOW/FLUTTER");
    setupLabel(tapeAgeLabel, "TAPE AGE");
    setupLabel(motorTorqueLabel, "MOTOR");
}

void TapeEchoEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Layer 1: Dark background
    g.fillAll(RE201Colours::background);

    // Layer 2: Brushed aluminum faceplate (full area minus small border)
    auto faceplateArea = bounds.reduced(2.0f);
    drawBrushedAluminum(g, faceplateArea);

    // Calculate the center panel area (where black frame + green panel go)
    const float headerHeight = 45.0f;
    const float footerHeight = 60.0f;
    const float leftMargin = 80.0f;
    const float rightMargin = 100.0f;

    auto centerArea = faceplateArea;
    centerArea.removeFromTop(headerHeight);
    centerArea.removeFromBottom(footerHeight);
    centerArea.removeFromLeft(leftMargin);
    centerArea.removeFromRight(rightMargin);

    // Layer 3: Black recessed frame
    drawBlackFrame(g, centerArea);

    // Layer 4: Green control panel (inset into black frame)
    auto greenPanelArea = centerArea.reduced(8.0f);
    drawGreenPanel(g, greenPanelArea);

    // Draw corner screws on green panel
    drawCornerScrews(g, greenPanelArea);

    // Draw header text
    drawLogoAndTitle(g, faceplateArea.removeFromTop(headerHeight));
}

void TapeEchoEditor::drawBrushedAluminum(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Base aluminum gradient (top lighter, bottom slightly darker)
    {
        juce::ColourGradient aluminumGradient(
            RE201Colours::aluminumLight, bounds.getX(), bounds.getY(),
            RE201Colours::aluminumDark, bounds.getX(), bounds.getBottom(), false);
        aluminumGradient.addColour(0.3, RE201Colours::aluminumMid);
        aluminumGradient.addColour(0.7, RE201Colours::aluminumMid.darker(0.05f));
        g.setGradientFill(aluminumGradient);
        g.fillRect(bounds);
    }

    // Horizontal brush stroke texture
    juce::Random random(54321);  // Consistent seed for stable texture

    for (float y = bounds.getY(); y < bounds.getBottom(); y += 1.0f)
    {
        // Variation in each stroke
        float lineAlpha = 0.02f + random.nextFloat() * 0.08f;
        bool isHighlight = random.nextFloat() > 0.94f;
        bool isScratch = random.nextFloat() > 0.98f;

        if (isHighlight)
        {
            g.setColour(RE201Colours::aluminumHighlight.withAlpha(0.25f));
        }
        else if (isScratch)
        {
            g.setColour(RE201Colours::aluminumShadow.withAlpha(0.15f));
        }
        else
        {
            g.setColour(RE201Colours::aluminumLight.withAlpha(lineAlpha));
        }

        g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());
    }

    // Top edge bright highlight
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawLine(bounds.getX(), bounds.getY() + 1.0f, bounds.getRight(), bounds.getY() + 1.0f, 1.0f);

    // Bottom edge shadow
    g.setColour(RE201Colours::aluminumShadow);
    g.drawLine(bounds.getX(), bounds.getBottom() - 1.0f, bounds.getRight(), bounds.getBottom() - 1.0f, 1.5f);

    // Outer border
    g.setColour(RE201Colours::aluminumShadow.darker(0.3f));
    g.drawRect(bounds, 1.0f);
}

void TapeEchoEditor::drawBlackFrame(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Dark recessed frame that surrounds the green panel
    g.setColour(RE201Colours::frameBlack);
    g.fillRoundedRectangle(bounds, 3.0f);

    // Inner shadow on top and left (creates recessed look)
    {
        juce::ColourGradient topShadow(
            RE201Colours::frameShadow, bounds.getX(), bounds.getY(),
            juce::Colours::transparentBlack, bounds.getX(), bounds.getY() + 12.0f, false);
        g.setGradientFill(topShadow);
        g.fillRoundedRectangle(bounds, 3.0f);
    }
    {
        juce::ColourGradient leftShadow(
            RE201Colours::frameShadow.withAlpha(0.4f), bounds.getX(), bounds.getY(),
            juce::Colours::transparentBlack, bounds.getX() + 12.0f, bounds.getY(), false);
        g.setGradientFill(leftShadow);
        g.fillRoundedRectangle(bounds, 3.0f);
    }

    // Light catch on bottom-right edges
    g.setColour(RE201Colours::frameHighlight.withAlpha(0.12f));
    g.drawLine(bounds.getX() + 8.0f, bounds.getBottom() - 2.0f,
               bounds.getRight() - 3.0f, bounds.getBottom() - 2.0f, 1.0f);
    g.drawLine(bounds.getRight() - 2.0f, bounds.getY() + 8.0f,
               bounds.getRight() - 2.0f, bounds.getBottom() - 3.0f, 1.0f);
}

void TapeEchoEditor::drawGreenPanel(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Main green fill
    g.setColour(RE201Colours::panelGreen);
    g.fillRoundedRectangle(bounds, 3.0f);

    // Subtle horizontal line texture (PCB look)
    for (float y = bounds.getY(); y < bounds.getBottom(); y += 2.5f)
    {
        g.setColour(RE201Colours::panelGreenDark.withAlpha(0.08f));
        g.drawHorizontalLine(static_cast<int>(y), bounds.getX() + 3.0f, bounds.getRight() - 3.0f);
    }

    // Inner shadow on top and left (recessed into black frame)
    {
        juce::ColourGradient topShadow(
            RE201Colours::panelGreenShadow.withAlpha(0.4f), bounds.getX(), bounds.getY(),
            juce::Colours::transparentBlack, bounds.getX(), bounds.getY() + 15.0f, false);
        g.setGradientFill(topShadow);
        g.fillRoundedRectangle(bounds, 3.0f);
    }
    {
        juce::ColourGradient leftShadow(
            RE201Colours::panelGreenShadow.withAlpha(0.25f), bounds.getX(), bounds.getY(),
            juce::Colours::transparentBlack, bounds.getX() + 15.0f, bounds.getY(), false);
        g.setGradientFill(leftShadow);
        g.fillRoundedRectangle(bounds, 3.0f);
    }

    // Light highlight on bottom edge
    g.setColour(RE201Colours::panelGreenLight.withAlpha(0.15f));
    g.drawLine(bounds.getX() + 8.0f, bounds.getBottom() - 2.0f,
               bounds.getRight() - 8.0f, bounds.getBottom() - 2.0f, 1.0f);

    // Border
    g.setColour(RE201Colours::panelGreenDark);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
}

void TapeEchoEditor::drawCornerScrews(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const float screwRadius = 4.0f;
    const float inset = 10.0f;

    // Screw positions
    juce::Point<float> screwPositions[] = {
        { bounds.getX() + inset, bounds.getY() + inset },
        { bounds.getRight() - inset, bounds.getY() + inset },
        { bounds.getX() + inset, bounds.getBottom() - inset },
        { bounds.getRight() - inset, bounds.getBottom() - inset }
    };

    for (const auto& pos : screwPositions)
    {
        // Screw shadow
        g.setColour(RE201Colours::shadow);
        g.fillEllipse(pos.x - screwRadius + 1.0f, pos.y - screwRadius + 1.5f,
                      screwRadius * 2, screwRadius * 2);

        // Screw head gradient
        juce::ColourGradient screwGrad(
            RE201Colours::screwHighlight, pos.x - screwRadius * 0.3f, pos.y - screwRadius * 0.3f,
            RE201Colours::screwShadow, pos.x + screwRadius * 0.5f, pos.y + screwRadius * 0.5f, true);
        screwGrad.addColour(0.5, RE201Colours::screwHead);
        g.setGradientFill(screwGrad);
        g.fillEllipse(pos.x - screwRadius, pos.y - screwRadius, screwRadius * 2, screwRadius * 2);

        // Phillips slot (cross)
        g.setColour(RE201Colours::screwSlot);
        const float slotWidth = 1.2f;
        const float slotLength = screwRadius * 1.1f;

        g.fillRect(pos.x - slotLength * 0.5f, pos.y - slotWidth * 0.5f, slotLength, slotWidth);
        g.fillRect(pos.x - slotWidth * 0.5f, pos.y - slotLength * 0.5f, slotWidth, slotLength);

        // Screw edge
        g.setColour(RE201Colours::screwShadow);
        g.drawEllipse(pos.x - screwRadius, pos.y - screwRadius, screwRadius * 2, screwRadius * 2, 0.5f);
    }
}

void TapeEchoEditor::drawLogoAndTitle(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    auto textBounds = bounds.reduced(15.0f, 8.0f);

    // Embossed "SPACE ECHO" title (left side)
    g.setFont(juce::Font(juce::FontOptions(20.0f).withStyle("Bold")));

    // Shadow (offset down-right)
    g.setColour(RE201Colours::aluminumShadow);
    g.drawText("SPACE ECHO", textBounds.translated(1.0f, 1.0f), juce::Justification::centredLeft);

    // Highlight (offset up-left)
    g.setColour(RE201Colours::aluminumHighlight.withAlpha(0.5f));
    g.drawText("SPACE ECHO", textBounds.translated(-0.5f, -0.5f), juce::Justification::centredLeft);

    // Main text
    g.setColour(RE201Colours::textOnAluminum);
    g.drawText("SPACE ECHO", textBounds, juce::Justification::centredLeft);

    // "LUNA CO. AUDIO" on right
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.setColour(RE201Colours::textOnAluminum);
    g.drawText("LUNA CO. AUDIO", textBounds, juce::Justification::centredRight);
}

void TapeEchoEditor::resized()
{
    auto bounds = getLocalBounds();

    // Calculate areas matching paint()
    const int headerHeight = 45;
    const int footerHeight = 60;
    const int leftMargin = 80;
    const int rightMargin = 100;

    auto headerBounds = bounds.removeFromTop(headerHeight);
    auto footerBounds = bounds.removeFromBottom(footerHeight);

    // Preset selector in header center
    auto presetBounds = headerBounds.withSizeKeepingCentre(140, 22);
    presetSelector.setBounds(presetBounds);

    // Left aluminum area: Input Volume knob
    auto leftAluminum = bounds.removeFromLeft(leftMargin);
    {
        auto inputArea = leftAluminum.reduced(8, 15);
        inputVolumeLabel.setBounds(inputArea.removeFromTop(12));
        inputVolumeKnob.setBounds(inputArea.withSizeKeepingCentre(52, 52));
    }

    // Right aluminum area: VU Meter
    auto rightAluminum = bounds.removeFromRight(rightMargin);
    {
        auto vuArea = rightAluminum.reduced(6, 20);
        vuMeter.setBounds(vuArea);
    }

    // Green panel content area (matches black frame inset)
    auto greenPanelContent = bounds.reduced(12, 10);

    // Left side of green panel: Mode Selector (HEAD SELECT)
    auto modeSelectorArea = greenPanelContent.removeFromLeft(140);
    modeSelector.setBounds(modeSelectorArea.reduced(5, 5));

    // Remaining area for knobs - organize as 2 rows x 4 columns (like UAD Galaxy)
    auto knobsArea = greenPanelContent.reduced(8, 4);
    const int knobSize = 54;
    const int labelHeight = 12;

    // Top row: Echo Rate, Treble, Echo Pan (using Wow/Flutter), Reverb Pan (using Tape Age)
    // Bottom row: Feedback, Bass, Echo Vol, Reverb Vol
    // Using our available parameters, arrange as:
    // Top: Echo Rate, Feedback, Treble, Reverb Vol
    // Bottom: Echo Vol, Bass, Wow/Flutter, Tape Age

    int rowHeight = knobsArea.getHeight() / 2;
    int numKnobsPerRow = 4;
    int knobAreaWidth = knobsArea.getWidth() / numKnobsPerRow;

    // Top row
    auto topRow = knobsArea.removeFromTop(rowHeight);
    {
        // Echo Rate
        auto area1 = topRow.removeFromLeft(knobAreaWidth);
        repeatRateLabel.setBounds(area1.removeFromTop(labelHeight).reduced(2, 0));
        repeatRateKnob.setBounds(area1.withSizeKeepingCentre(knobSize, knobSize));

        // Feedback (Intensity)
        auto area2 = topRow.removeFromLeft(knobAreaWidth);
        intensityLabel.setBounds(area2.removeFromTop(labelHeight).reduced(2, 0));
        intensityKnob.setBounds(area2.withSizeKeepingCentre(knobSize, knobSize));

        // Treble
        auto area3 = topRow.removeFromLeft(knobAreaWidth);
        trebleLabel.setBounds(area3.removeFromTop(labelHeight).reduced(2, 0));
        trebleKnob.setBounds(area3.withSizeKeepingCentre(knobSize, knobSize));

        // Reverb Vol
        auto area4 = topRow;
        reverbVolumeLabel.setBounds(area4.removeFromTop(labelHeight).reduced(2, 0));
        reverbVolumeKnob.setBounds(area4.withSizeKeepingCentre(knobSize, knobSize));
    }

    // Bottom row
    auto bottomRow = knobsArea;
    {
        // Echo Vol
        auto area1 = bottomRow.removeFromLeft(knobAreaWidth);
        echoVolumeLabel.setBounds(area1.removeFromTop(labelHeight).reduced(2, 0));
        echoVolumeKnob.setBounds(area1.withSizeKeepingCentre(knobSize, knobSize));

        // Bass
        auto area2 = bottomRow.removeFromLeft(knobAreaWidth);
        bassLabel.setBounds(area2.removeFromTop(labelHeight).reduced(2, 0));
        bassKnob.setBounds(area2.withSizeKeepingCentre(knobSize, knobSize));

        // Wow/Flutter
        auto area3 = bottomRow.removeFromLeft(knobAreaWidth);
        wowFlutterLabel.setBounds(area3.removeFromTop(labelHeight).reduced(2, 0));
        wowFlutterKnob.setBounds(area3.withSizeKeepingCentre(knobSize, knobSize));

        // Tape Age
        auto area4 = bottomRow;
        tapeAgeLabel.setBounds(area4.removeFromTop(labelHeight).reduced(2, 0));
        tapeAgeKnob.setBounds(area4.withSizeKeepingCentre(knobSize, knobSize));
    }

    // Footer: Motor and Stereo switch
    auto footerContent = footerBounds.reduced(15, 8);
    {
        // Motor knob on left
        auto motorArea = footerContent.removeFromLeft(100);
        motorTorqueLabel.setBounds(motorArea.removeFromTop(labelHeight).reduced(2, 0));
        motorTorqueKnob.setBounds(motorArea.withSizeKeepingCentre(42, 42));

        // Stereo toggle on right
        auto stereoArea = footerContent.removeFromRight(80);
        stereoSwitch.setBounds(stereoArea.withSizeKeepingCentre(45, 50));
    }
}

void TapeEchoEditor::layoutKnobWithLabel(juce::Slider& knob, juce::Label& label,
                                          juce::Rectangle<int> area, int labelHeight, int knobSize)
{
    // Label at top
    auto labelArea = area.removeFromTop(labelHeight);
    labelArea = labelArea.expanded(10, 0);
    label.setBounds(labelArea);

    // Center knob in remaining space
    auto knobArea = area.withSizeKeepingCentre(knobSize, knobSize);
    knob.setBounds(knobArea);
}

void TapeEchoEditor::timerCallback()
{
    float level = audioProcessor.getCurrentPeakLevel();
    vuMeter.setLevel(level);

    // Update mode selector
    int mode = static_cast<int>(audioProcessor.apvts.getRawParameterValue(
        TapeEchoProcessor::PARAM_MODE)->load());
    modeSelector.setMode(mode);

    // Update stereo switch state
    bool stereoState = audioProcessor.apvts.getRawParameterValue(
        TapeEchoProcessor::PARAM_STEREO_MODE)->load() > 0.5f;
    stereoSwitch.setToggleState(stereoState);
}

void TapeEchoEditor::comboBoxChanged(juce::ComboBox* comboBox)
{
    if (comboBox == &presetSelector)
    {
        int selection = presetSelector.getSelectedId();
        if (selection > 1)
        {
            size_t presetIndex = static_cast<size_t>(selection - 2);
            if (presetIndex < audioProcessor.getFactoryPresets().size())
            {
                audioProcessor.loadPreset(audioProcessor.getFactoryPresets()[presetIndex]);
            }
        }
    }
}

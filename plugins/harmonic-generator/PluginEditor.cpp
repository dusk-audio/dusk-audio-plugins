#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../../shared/LunaLookAndFeel.h"
#include <cmath>

//==============================================================================
// AnalogLookAndFeel Implementation - now inherits from Luna
HarmonicGeneratorAudioProcessorEditor::AnalogLookAndFeel::AnalogLookAndFeel()
{
    // Inherits Luna color scheme
    // Colors are already set in LunaLookAndFeel base class
}

HarmonicGeneratorAudioProcessorEditor::AnalogLookAndFeel::~AnalogLookAndFeel() = default;

// drawRotarySlider and drawToggleButton are inherited from LunaLookAndFeel

void HarmonicGeneratorAudioProcessorEditor::AnalogLookAndFeel::drawLinearSlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float, float,
    const juce::Slider::SliderStyle style, juce::Slider&)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        // Track
        g.setColour(juce::Colour(LunaLookAndFeel::PANEL_COLOR));
        g.fillRoundedRectangle(x, y + height * 0.4f, width, height * 0.2f, 2.0f);

        // Fill
        g.setColour(juce::Colour(LunaLookAndFeel::ACCENT_COLOR).darker(0.2f));
        g.fillRoundedRectangle(x, y + height * 0.4f, sliderPos - x, height * 0.2f, 2.0f);

        // Thumb
        g.setColour(juce::Colour(LunaLookAndFeel::ACCENT_COLOR));
        g.fillEllipse(sliderPos - 8, y + height * 0.5f - 8, 16, 16);
        g.setColour(juce::Colour(LunaLookAndFeel::BACKGROUND_COLOR));
        g.drawEllipse(sliderPos - 8, y + height * 0.5f - 8, 16, 16, 2.0f);
    }
}

//==============================================================================
// SpectrumDisplay Implementation
HarmonicGeneratorAudioProcessorEditor::SpectrumDisplay::SpectrumDisplay()
{
}

void HarmonicGeneratorAudioProcessorEditor::SpectrumDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(0xff0a0a0a));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Grid
    g.setColour(juce::Colour(0x20ffffff));
    for (int i = 1; i < 4; ++i)
    {
        float y = bounds.getY() + (bounds.getHeight() / 4.0f) * i;
        g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());
    }

    // Draw harmonic bars
    float barWidth = bounds.getWidth() / 6.0f;
    float barSpacing = barWidth * 0.2f;

    const char* labels[] = { "F", "2nd", "3rd", "4th", "5th" };

    for (int i = 0; i < 5; ++i)
    {
        float x = bounds.getX() + barSpacing + i * (barWidth + barSpacing);

        // Smooth the levels
        smoothedLevels[i] = smoothedLevels[i] * 0.8f + harmonicLevels[i] * 0.2f;
        float barHeight = smoothedLevels[i] * bounds.getHeight() * 0.9f;

        // Bar gradient
        juce::ColourGradient barGradient(
            juce::Colour(0xffff6b35), x, bounds.getBottom(),
            juce::Colour(0xff8b4513), x, bounds.getBottom() - barHeight, false);
        g.setGradientFill(barGradient);
        g.fillRoundedRectangle(x, bounds.getBottom() - barHeight, barWidth * 0.8f, barHeight, 2.0f);

        // Labels
        g.setColour(juce::Colour(0xff8a8a8a));
        g.setFont(10.0f);
        g.drawText(labels[i], x, bounds.getBottom() - 15, barWidth * 0.8f, 15, juce::Justification::centred);
    }

    // Border
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

void HarmonicGeneratorAudioProcessorEditor::SpectrumDisplay::updateSpectrum(const std::array<float, 5>& harmonics)
{
    harmonicLevels = harmonics;
    repaint();
}

//==============================================================================
// LevelMeter Implementation
HarmonicGeneratorAudioProcessorEditor::LevelMeter::LevelMeter()
{
}

void HarmonicGeneratorAudioProcessorEditor::LevelMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(0xff0a0a0a));
    g.fillRoundedRectangle(bounds, 2.0f);

    if (stereo)
    {
        // Draw stereo meters
        float meterWidth = bounds.getWidth() / 2.0f - 2;

        // Left meter
        smoothedLevelL = smoothedLevelL * 0.85f + levelL * 0.15f;
        float leftHeight = bounds.getHeight() * smoothedLevelL;

        juce::ColourGradient leftGradient(
            juce::Colour(0xff00ff00), 0, bounds.getBottom(),
            juce::Colour(0xffff0000), 0, bounds.getY(), false);
        leftGradient.addColour(0.7, juce::Colour(0xffffff00));

        g.setGradientFill(leftGradient);
        g.fillRoundedRectangle(bounds.getX(), bounds.getBottom() - leftHeight,
                              meterWidth, leftHeight, 1.0f);

        // Right meter
        smoothedLevelR = smoothedLevelR * 0.85f + levelR * 0.15f;
        float rightHeight = bounds.getHeight() * smoothedLevelR;

        g.setGradientFill(leftGradient);
        g.fillRoundedRectangle(bounds.getX() + meterWidth + 2, bounds.getBottom() - rightHeight,
                              meterWidth, rightHeight, 1.0f);
    }
    else
    {
        // Draw mono meter
        smoothedLevelL = smoothedLevelL * 0.85f + levelL * 0.15f;
        float meterHeight = bounds.getHeight() * smoothedLevelL;

        juce::ColourGradient gradient(
            juce::Colour(0xff00ff00), 0, bounds.getBottom(),
            juce::Colour(0xffff0000), 0, bounds.getY(), false);
        gradient.addColour(0.7, juce::Colour(0xffffff00));

        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bounds.getX(), bounds.getBottom() - meterHeight,
                              bounds.getWidth(), meterHeight, 2.0f);
    }

    // Border
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
}

void HarmonicGeneratorAudioProcessorEditor::LevelMeter::setLevel(float newLevel)
{
    levelL = juce::jlimit(0.0f, 1.0f, newLevel);
    stereo = false;
    repaint();
}

void HarmonicGeneratorAudioProcessorEditor::LevelMeter::setStereoLevels(float left, float right)
{
    levelL = juce::jlimit(0.0f, 1.0f, left);
    levelR = juce::jlimit(0.0f, 1.0f, right);
    stereo = true;
    repaint();
}

//==============================================================================
// Main Editor Implementation
HarmonicGeneratorAudioProcessorEditor::HarmonicGeneratorAudioProcessorEditor(HarmonicGeneratorAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&customLookAndFeel);

    // Setup harmonic controls
    setupSlider(secondHarmonicSlider, secondHarmonicLabel, "2nd");
    setupSlider(thirdHarmonicSlider, thirdHarmonicLabel, "3rd");
    setupSlider(fourthHarmonicSlider, fourthHarmonicLabel, "4th");
    setupSlider(fifthHarmonicSlider, fifthHarmonicLabel, "5th");

    // Setup global controls
    setupSlider(evenHarmonicsSlider, evenHarmonicsLabel, "Even");
    setupSlider(oddHarmonicsSlider, oddHarmonicsLabel, "Odd");

    // Setup character controls
    setupSlider(warmthSlider, warmthLabel, "Warmth");
    setupSlider(brightnessSlider, brightnessLabel, "Brightness");

    // Setup I/O controls
    setupSlider(driveSlider, driveLabel, "Drive");
    setupSlider(outputGainSlider, outputGainLabel, "Output");
    setupSlider(mixSlider, mixLabel, "Mix", juce::Slider::LinearHorizontal);

    // Setup oversampling button
    oversamplingButton.setButtonText("2x Oversampling");
    addAndMakeVisible(oversamplingButton);

    // Setup preset selector
    presetSelector.addItem("-- Select Preset --", 1);
    presetSelector.addSectionHeading("Tape Machines");
    presetSelector.addItem("Studer A800", 2);
    presetSelector.addItem("Ampex ATR-102", 3);
    presetSelector.addItem("Tascam Porta", 4);
    presetSelector.addSectionHeading("Tubes");
    presetSelector.addItem("Fairchild 670", 5);
    presetSelector.addItem("Pultec EQP-1A", 6);
    presetSelector.addItem("UA 610", 7);
    presetSelector.addSectionHeading("Transistors");
    presetSelector.addItem("Neve 1073", 8);
    presetSelector.addItem("API 2500", 9);
    presetSelector.addItem("SSL 4000E", 10);
    presetSelector.addSectionHeading("Special");
    presetSelector.addItem("Culture Vulture", 11);
    presetSelector.addItem("Decapitator", 12);
    presetSelector.addItem("HG-2 Black Box", 13);
    presetSelector.setSelectedId(1);
    presetSelector.addListener(this);
    addAndMakeVisible(presetSelector);

    presetLabel.setText("Saturation Type", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centred);
    presetLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    presetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
    addAndMakeVisible(presetLabel);

    // Setup visual displays
    addAndMakeVisible(spectrumDisplay);
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);

    // Create parameter attachments
    secondHarmonicAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.secondHarmonic, secondHarmonicSlider);
    thirdHarmonicAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.thirdHarmonic, thirdHarmonicSlider);
    fourthHarmonicAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.fourthHarmonic, fourthHarmonicSlider);
    fifthHarmonicAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.fifthHarmonic, fifthHarmonicSlider);
    evenHarmonicsAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.evenHarmonics, evenHarmonicsSlider);
    oddHarmonicsAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.oddHarmonics, oddHarmonicsSlider);
    warmthAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.warmth, warmthSlider);
    brightnessAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.brightness, brightnessSlider);
    driveAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.drive, driveSlider);
    outputGainAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.outputGain, outputGainSlider);
    mixAttachment = std::make_unique<juce::SliderParameterAttachment>(
        *audioProcessor.wetDryMix, mixSlider);
    oversamplingAttachment = std::make_unique<juce::ButtonParameterAttachment>(
        *audioProcessor.oversamplingSwitch, oversamplingButton);

    // Add listeners
    secondHarmonicSlider.addListener(this);
    thirdHarmonicSlider.addListener(this);
    fourthHarmonicSlider.addListener(this);
    fifthHarmonicSlider.addListener(this);

    setSize(750, 500);
    startTimerHz(30);
}

HarmonicGeneratorAudioProcessorEditor::~HarmonicGeneratorAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void HarmonicGeneratorAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background gradient
    juce::ColourGradient bgGradient(
        juce::Colour(0xff2a2a2a), 0, 0,
        juce::Colour(0xff1a1a1a), 0, getHeight(), false);
    g.setGradientFill(bgGradient);
    g.fillAll();

    // Title with better styling
    g.setColour(juce::Colour(0xffff6b35));
    g.setFont(juce::Font(26.0f).withStyle(juce::Font::bold));
    g.drawText("HARMONIC GENERATOR", getLocalBounds().removeFromTop(50),
              juce::Justification::centred);

    // Section dividers
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawLine(0, 50, getWidth(), 50, 2.0f);
    g.drawLine(0, 280, getWidth(), 280, 1.0f);
    g.drawLine(0, 400, getWidth(), 400, 1.0f);

    // Section labels with boxes
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRoundedRectangle(15, 55, 110, 25, 3);
    g.fillRoundedRectangle(15, 285, 110, 25, 3);
    g.fillRoundedRectangle(15, 405, 80, 25, 3);

    g.setColour(juce::Colour(0xff8a8a8a));
    g.setFont(11.0f);
    g.drawText("HARMONICS", 15, 55, 110, 25, juce::Justification::centred);
    g.drawText("CHARACTER", 15, 285, 110, 25, juce::Justification::centred);
    g.drawText("OUTPUT", 15, 405, 80, 25, juce::Justification::centred);
}

void HarmonicGeneratorAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(70);

    // Harmonic controls section
    auto harmonicsSection = bounds.removeFromTop(200);
    auto knobSize = 70;
    auto knobSpacing = 85;

    // Individual harmonic knobs
    secondHarmonicSlider.setBounds(30, harmonicsSection.getY() + 20, knobSize, knobSize);
    thirdHarmonicSlider.setBounds(30 + knobSpacing, harmonicsSection.getY() + 20, knobSize, knobSize);
    fourthHarmonicSlider.setBounds(30 + knobSpacing * 2, harmonicsSection.getY() + 20, knobSize, knobSize);
    fifthHarmonicSlider.setBounds(30 + knobSpacing * 3, harmonicsSection.getY() + 20, knobSize, knobSize);

    // Global harmonic controls
    evenHarmonicsSlider.setBounds(30, harmonicsSection.getY() + 110, knobSize, knobSize);
    oddHarmonicsSlider.setBounds(30 + knobSpacing, harmonicsSection.getY() + 110, knobSize, knobSize);

    // Spectrum display
    spectrumDisplay.setBounds(400, harmonicsSection.getY() + 20, 320, 160);

    // Character section
    auto characterSection = bounds.removeFromTop(110);
    warmthSlider.setBounds(30, characterSection.getY() + 20, knobSize, knobSize);
    brightnessSlider.setBounds(30 + knobSpacing, characterSection.getY() + 20, knobSize, knobSize);

    // Oversampling button
    oversamplingButton.setBounds(250, characterSection.getY() + 40, 150, 30);

    // Preset selector
    presetLabel.setBounds(430, characterSection.getY() + 10, 120, 25);
    presetSelector.setBounds(430, characterSection.getY() + 35, 200, 30);

    // Output section
    auto outputSection = bounds.removeFromTop(90);
    driveSlider.setBounds(30, outputSection.getY() + 10, knobSize, knobSize);
    outputGainSlider.setBounds(30 + knobSpacing, outputSection.getY() + 10, knobSize, knobSize);

    // Mix slider
    mixSlider.setBounds(220, outputSection.getY() + 30, 200, 30);

    // Meters
    inputMeter.setBounds(450, outputSection.getY() + 10, 30, 70);
    outputMeter.setBounds(490, outputSection.getY() + 10, 30, 70);
}

void HarmonicGeneratorAudioProcessorEditor::timerCallback()
{
    // Update meters
    inputMeter.setStereoLevels(audioProcessor.inputLevelL, audioProcessor.inputLevelR);
    outputMeter.setStereoLevels(audioProcessor.outputLevelL, audioProcessor.outputLevelR);

    // Update spectrum display
    std::array<float, 5> harmonics = {
        1.0f,  // Fundamental (always shown at full)
        audioProcessor.secondHarmonic->get(),
        audioProcessor.thirdHarmonic->get(),
        audioProcessor.fourthHarmonic->get(),
        audioProcessor.fifthHarmonic->get()
    };
    spectrumDisplay.updateSpectrum(harmonics);
}

void HarmonicGeneratorAudioProcessorEditor::sliderValueChanged(juce::Slider*)
{
    // Handled by parameter attachments
}

void HarmonicGeneratorAudioProcessorEditor::buttonClicked(juce::Button*)
{
    // Handled by parameter attachments
}

void HarmonicGeneratorAudioProcessorEditor::comboBoxChanged(juce::ComboBox* comboBox)
{
    if (comboBox == &presetSelector)
    {
        int presetId = presetSelector.getSelectedId();

        // Apply preset harmonic values based on selection
        switch (presetId)
        {
            case 2:  // Studer A800 - Warm tape with prominent 2nd harmonic
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.30f);  // Strong 2nd
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.10f);   // Some 3rd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.05f);  // Subtle 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.02f);   // Minimal 5th
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.7f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.3f);
                audioProcessor.warmth->setValueNotifyingHost(0.7f);
                audioProcessor.brightness->setValueNotifyingHost(0.3f);
                break;

            case 3:  // Ampex ATR-102 - Clean mastering tape
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.20f);  // Moderate 2nd
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.08f);   // Low 3rd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.03f);  // Very subtle 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.01f);   // Minimal 5th
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.75f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.25f);
                audioProcessor.warmth->setValueNotifyingHost(0.6f);
                audioProcessor.brightness->setValueNotifyingHost(0.4f);
                break;

            case 4:  // Tascam Porta - Lo-fi cassette
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.50f);  // Heavy 2nd
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.30f);   // Strong 3rd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.20f);  // Notable 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.10f);   // Some 5th
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.6f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.4f);
                audioProcessor.warmth->setValueNotifyingHost(0.8f);
                audioProcessor.brightness->setValueNotifyingHost(0.2f);
                break;

            case 5:  // Fairchild 670 - Tube compressor warmth
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.40f);  // Strong even harmonics
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.05f);   // Minimal odd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.02f);  // Very subtle
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.01f);   // Almost none
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.9f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.1f);
                audioProcessor.warmth->setValueNotifyingHost(0.85f);
                audioProcessor.brightness->setValueNotifyingHost(0.3f);
                break;

            case 6:  // Pultec EQP-1A - Tube EQ warmth
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.25f);  // Moderate 2nd
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.03f);   // Very low 3rd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.01f);  // Minimal 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.0f);    // None
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.85f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.15f);
                audioProcessor.warmth->setValueNotifyingHost(0.9f);
                audioProcessor.brightness->setValueNotifyingHost(0.4f);
                break;

            case 7:  // UA 610 - Tube preamp
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.35f);  // Good amount of 2nd
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.10f);   // Some 3rd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.05f);  // Subtle 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.02f);   // Minimal 5th
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.8f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.2f);
                audioProcessor.warmth->setValueNotifyingHost(0.75f);
                audioProcessor.brightness->setValueNotifyingHost(0.35f);
                break;

            case 8:  // Neve 1073 - Class A transistor
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.15f);  // Less even
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.25f);   // More odd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.05f);  // Some 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.08f);   // Notable 5th
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.4f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.6f);
                audioProcessor.warmth->setValueNotifyingHost(0.5f);
                audioProcessor.brightness->setValueNotifyingHost(0.6f);
                break;

            case 9:  // API 2500 - VCA compression
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.10f);  // Low even
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.20f);   // More odd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.03f);  // Minimal 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.05f);   // Some 5th
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.35f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.65f);
                audioProcessor.warmth->setValueNotifyingHost(0.4f);
                audioProcessor.brightness->setValueNotifyingHost(0.7f);
                break;

            case 10:  // SSL 4000E - Console aggression
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.08f);  // Low even
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.30f);   // Strong odd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.05f);  // Some 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.10f);   // Notable 5th
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.3f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.7f);
                audioProcessor.warmth->setValueNotifyingHost(0.3f);
                audioProcessor.brightness->setValueNotifyingHost(0.8f);
                break;

            case 11:  // Culture Vulture - Tube distortion
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.60f);  // Heavy 2nd
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.40f);   // Strong 3rd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.30f);  // Notable 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.20f);   // Good amount of 5th
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.6f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.4f);
                audioProcessor.warmth->setValueNotifyingHost(0.7f);
                audioProcessor.brightness->setValueNotifyingHost(0.5f);
                break;

            case 12:  // Decapitator - Analog saturation
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.40f);  // Balanced
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.35f);   // Balanced
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.20f);  // Moderate 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.15f);   // Moderate 5th
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.5f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.5f);
                audioProcessor.warmth->setValueNotifyingHost(0.6f);
                audioProcessor.brightness->setValueNotifyingHost(0.6f);
                break;

            case 13:  // HG-2 Black Box - Tube/transformer
                audioProcessor.secondHarmonic->setValueNotifyingHost(0.45f);  // Strong 2nd
                audioProcessor.thirdHarmonic->setValueNotifyingHost(0.08f);   // Low odd
                audioProcessor.fourthHarmonic->setValueNotifyingHost(0.04f);  // Minimal 4th
                audioProcessor.fifthHarmonic->setValueNotifyingHost(0.02f);   // Almost none
                audioProcessor.evenHarmonics->setValueNotifyingHost(0.82f);
                audioProcessor.oddHarmonics->setValueNotifyingHost(0.18f);
                audioProcessor.warmth->setValueNotifyingHost(0.8f);
                audioProcessor.brightness->setValueNotifyingHost(0.4f);
                break;

            default:
                break;
        }
    }
}

void HarmonicGeneratorAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label,
                                                       const juce::String& text,
                                                       juce::Slider::SliderStyle style)
{
    slider.setSliderStyle(style);
    if (style == juce::Slider::RotaryVerticalDrag)
    {
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);  // Wider text box
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffd4d4d4));
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0a0a0a));
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff3a3a3a));
    }
    else
    {
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    }
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    label.setFont(juce::Font(10.0f));
    label.attachToComponent(&slider, false);
    addAndMakeVisible(label);
}
/*
  ==============================================================================

    ModernCompressorPanels.h - UI for Digital and Multiband compressor modes

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "UniversalCompressor.h"

//==============================================================================
// Modern Look and Feel for Digital/Multiband modes
//==============================================================================
class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel()
    {
        // Modern flat design colors
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1e1e1e));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff00d4ff));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff0099cc));
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00d4ff));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));

        setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff00d4ff));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
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

        // Modern flat background
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillEllipse(rx, ry, rw, rw);

        // Colored arc showing value
        juce::Path arc;
        arc.addArc(rx + 2, ry + 2, rw - 4, rw - 4, rotaryStartAngle, angle, true);
        g.setColour(juce::Colour(0xff00d4ff));
        g.strokePath(arc, juce::PathStrokeType(3.0f));

        // Center dot
        g.setColour(juce::Colour(0xff2a2a2a));
        g.fillEllipse(centreX - 4, centreY - 4, 8, 8);

        // Value indicator line
        juce::Path pointer;
        pointer.startNewSubPath(centreX, centreY);
        pointer.lineTo(centreX + (radius - 10) * std::cos(angle),
                      centreY + (radius - 10) * std::sin(angle));
        g.setColour(juce::Colours::white);
        g.strokePath(pointer, juce::PathStrokeType(2.0f));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearVertical)
        {
            // Modern vertical fader
            auto trackWidth = 6.0f;
            auto trackX = x + width * 0.5f - trackWidth * 0.5f;

            // Background track
            g.setColour(juce::Colour(0xff1a1a1a));
            g.fillRoundedRectangle(trackX, y, trackWidth, height, 3.0f);

            // Filled portion
            auto fillHeight = sliderPos * height;
            g.setColour(juce::Colour(0xff00d4ff));
            g.fillRoundedRectangle(trackX, y + height - fillHeight, trackWidth, fillHeight, 3.0f);

            // Thumb
            auto thumbY = y + (1.0f - sliderPos) * height;
            g.setColour(juce::Colours::white);
            g.fillEllipse(x + width * 0.5f - 8, thumbY - 8, 16, 16);
            g.setColour(juce::Colour(0xff00d4ff));
            g.fillEllipse(x + width * 0.5f - 6, thumbY - 6, 12, 12);
        }
        else
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                            minSliderPos, maxSliderPos, style, slider);
        }
    }
};

//==============================================================================
// Digital Compressor Panel
//==============================================================================
class DigitalCompressorPanel : public juce::Component
{
public:
    DigitalCompressorPanel(juce::AudioProcessorValueTreeState& apvts)
        : parameters(apvts)
    {
        // Main compression controls
        addAndMakeVisible(thresholdSlider);
        thresholdSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        thresholdSlider.setRange(-60.0, 0.0, 0.1);
        thresholdSlider.setTextValueSuffix(" dB");
        thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        addAndMakeVisible(ratioSlider);
        ratioSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        ratioSlider.setRange(1.0, 100.0, 0.1);
        ratioSlider.setSkewFactorFromMidPoint(10.0);
        ratioSlider.setTextValueSuffix(":1");
        ratioSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        addAndMakeVisible(kneeSlider);
        kneeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        kneeSlider.setRange(0.0, 20.0, 0.1);
        kneeSlider.setTextValueSuffix(" dB");
        kneeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Time controls
        addAndMakeVisible(attackSlider);
        attackSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        attackSlider.setRange(0.01, 500.0, 0.01);
        attackSlider.setSkewFactorFromMidPoint(5.0);
        attackSlider.setTextValueSuffix(" ms");
        attackSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        addAndMakeVisible(releaseSlider);
        releaseSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        releaseSlider.setRange(1.0, 5000.0, 1.0);
        releaseSlider.setSkewFactorFromMidPoint(500.0);
        releaseSlider.setTextValueSuffix(" ms");
        releaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Lookahead
        addAndMakeVisible(lookaheadSlider);
        lookaheadSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        lookaheadSlider.setRange(0.0, 10.0, 0.1);
        lookaheadSlider.setTextValueSuffix(" ms");
        lookaheadSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Mix control
        addAndMakeVisible(mixSlider);
        mixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        mixSlider.setRange(0.0, 100.0, 1.0);
        mixSlider.setTextValueSuffix(" %");
        mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Output
        addAndMakeVisible(outputSlider);
        outputSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        outputSlider.setRange(-24.0, 24.0, 0.1);
        outputSlider.setTextValueSuffix(" dB");
        outputSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Advanced features
        addAndMakeVisible(adaptiveReleaseButton);
        adaptiveReleaseButton.setButtonText("Adaptive Release");

        addAndMakeVisible(sidechainListenButton);
        sidechainListenButton.setButtonText("SC Listen");

        // Sidechain EQ button (opens popup)
        addAndMakeVisible(sidechainEQButton);
        sidechainEQButton.setButtonText("Sidechain EQ");
        sidechainEQButton.onClick = [this] { showSidechainEQ(); };

        // Labels
        createLabels();

        // Create parameter attachments
        thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "digital_threshold", thresholdSlider);
        ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "digital_ratio", ratioSlider);
        kneeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "digital_knee", kneeSlider);
        attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "digital_attack", attackSlider);
        releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "digital_release", releaseSlider);
        lookaheadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "digital_lookahead", lookaheadSlider);
        mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "digital_mix", mixSlider);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "digital_output", outputSlider);
        adaptiveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            parameters, "digital_adaptive", adaptiveReleaseButton);
        listenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            parameters, "digital_sidechain_listen", sidechainListenButton);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);

        // Top row - main controls
        auto topRow = area.removeFromTop(100);
        auto knobWidth = topRow.getWidth() / 5;

        thresholdSlider.setBounds(topRow.removeFromLeft(knobWidth).reduced(5));
        ratioSlider.setBounds(topRow.removeFromLeft(knobWidth).reduced(5));
        kneeSlider.setBounds(topRow.removeFromLeft(knobWidth).reduced(5));
        attackSlider.setBounds(topRow.removeFromLeft(knobWidth).reduced(5));
        releaseSlider.setBounds(topRow.removeFromLeft(knobWidth).reduced(5));

        // Middle row
        area.removeFromTop(20);
        auto midRow = area.removeFromTop(100);
        knobWidth = midRow.getWidth() / 3;

        lookaheadSlider.setBounds(midRow.removeFromLeft(knobWidth).reduced(5));
        mixSlider.setBounds(midRow.removeFromLeft(knobWidth).reduced(5));
        outputSlider.setBounds(midRow.removeFromLeft(knobWidth).reduced(5));

        // Bottom row - buttons
        area.removeFromTop(20);
        auto buttonRow = area.removeFromTop(30);
        auto buttonWidth = buttonRow.getWidth() / 3;

        adaptiveReleaseButton.setBounds(buttonRow.removeFromLeft(buttonWidth).reduced(5, 0));
        sidechainListenButton.setBounds(buttonRow.removeFromLeft(buttonWidth).reduced(5, 0));
        sidechainEQButton.setBounds(buttonRow.removeFromLeft(buttonWidth).reduced(5, 0));
    }

    void paint(juce::Graphics& g) override
    {
        // Modern gradient background
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff1a1a1a), 0, 0,
            juce::Colour(0xff0d0d0d), 0, getHeight(), false));
        g.fillAll();

        // Section dividers
        g.setColour(juce::Colour(0xff2a2a2a));
        g.drawLine(0, 120, getWidth(), 120, 1.0f);
        g.drawLine(0, 240, getWidth(), 240, 1.0f);

        // Title
        g.setColour(juce::Colour(0xff00d4ff));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("DIGITAL COMPRESSOR", 0, 5, getWidth(), 20, juce::Justification::centred);
    }

private:
    juce::AudioProcessorValueTreeState& parameters;

    juce::Slider thresholdSlider, ratioSlider, kneeSlider;
    juce::Slider attackSlider, releaseSlider, lookaheadSlider;
    juce::Slider mixSlider, outputSlider;

    juce::ToggleButton adaptiveReleaseButton, sidechainListenButton;
    juce::TextButton sidechainEQButton;

    std::vector<std::unique_ptr<juce::Label>> labels;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> kneeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lookaheadAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> adaptiveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment;

    void createLabels()
    {
        addLabel("Threshold", thresholdSlider);
        addLabel("Ratio", ratioSlider);
        addLabel("Knee", kneeSlider);
        addLabel("Attack", attackSlider);
        addLabel("Release", releaseSlider);
        addLabel("Lookahead", lookaheadSlider);
        addLabel("Mix", mixSlider);
        addLabel("Output", outputSlider);
    }

    void addLabel(const juce::String& text, juce::Component& component)
    {
        auto label = std::make_unique<juce::Label>(text, text);
        label->attachToComponent(&component, false);
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
        addAndMakeVisible(*label);
        labels.push_back(std::move(label));
    }

    void showSidechainEQ()
    {
        // Would open a popup window with 4-band parametric EQ
    }
};

//==============================================================================
// Multiband Compressor Panel
//==============================================================================
class MultibandCompressorPanel : public juce::Component
{
public:
    MultibandCompressorPanel(juce::AudioProcessorValueTreeState& apvts)
        : parameters(apvts)
    {
        // Band selector
        addAndMakeVisible(bandSelector);
        bandSelector.addItem("Low", 1);
        bandSelector.addItem("Low-Mid", 2);
        bandSelector.addItem("High-Mid", 3);
        bandSelector.addItem("High", 4);
        bandSelector.setSelectedId(1);
        bandSelector.onChange = [this] { updateBandControls(); };

        // Crossover frequency sliders
        for (int i = 0; i < 3; ++i)
        {
            crossoverSliders[i].setSliderStyle(juce::Slider::LinearVertical);
            crossoverSliders[i].setRange(20.0, 20000.0, 1.0);
            crossoverSliders[i].setSkewFactorFromMidPoint(1000.0);
            crossoverSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
            addAndMakeVisible(crossoverSliders[i]);
        }
        crossoverSliders[0].setValue(200.0);
        crossoverSliders[1].setValue(2000.0);
        crossoverSliders[2].setValue(8000.0);

        // Per-band controls
        addAndMakeVisible(bandThreshold);
        bandThreshold.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        bandThreshold.setRange(-60.0, 0.0, 0.1);
        bandThreshold.setTextValueSuffix(" dB");

        addAndMakeVisible(bandRatio);
        bandRatio.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        bandRatio.setRange(1.0, 20.0, 0.1);
        bandRatio.setTextValueSuffix(":1");

        addAndMakeVisible(bandAttack);
        bandAttack.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        bandAttack.setRange(0.1, 100.0, 0.1);
        bandAttack.setSkewFactorFromMidPoint(10.0);
        bandAttack.setTextValueSuffix(" ms");

        addAndMakeVisible(bandRelease);
        bandRelease.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        bandRelease.setRange(10.0, 1000.0, 1.0);
        bandRelease.setSkewFactorFromMidPoint(100.0);
        bandRelease.setTextValueSuffix(" ms");

        addAndMakeVisible(bandMakeup);
        bandMakeup.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        bandMakeup.setRange(-12.0, 12.0, 0.1);
        bandMakeup.setTextValueSuffix(" dB");

        // Band bypass/solo
        addAndMakeVisible(bandBypass);
        bandBypass.setButtonText("Bypass");

        addAndMakeVisible(bandSolo);
        bandSolo.setButtonText("Solo");

        // Global controls
        addAndMakeVisible(globalOutput);
        globalOutput.setSliderStyle(juce::Slider::LinearVertical);
        globalOutput.setRange(-24.0, 24.0, 0.1);
        globalOutput.setTextValueSuffix(" dB");

        // Spectrum analyzer placeholder
        addAndMakeVisible(spectrumDisplay);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);

        // Top: band selector
        auto topBar = area.removeFromTop(30);
        bandSelector.setBounds(topBar.removeFromLeft(150));

        // Spectrum display area
        auto spectrumArea = area.removeFromTop(150);
        spectrumDisplay.setBounds(spectrumArea);

        // Crossover sliders on the left
        auto crossoverArea = area.removeFromLeft(150);
        auto sliderHeight = crossoverArea.getHeight() - 40;
        auto sliderWidth = 40;

        for (int i = 0; i < 3; ++i)
        {
            crossoverSliders[i].setBounds(10 + i * 45, 20, sliderWidth, sliderHeight);
        }

        // Band controls in center
        auto controlArea = area.removeFromLeft(400);
        auto knobSize = 70;
        auto row1 = controlArea.removeFromTop(100);

        bandThreshold.setBounds(row1.removeFromLeft(knobSize).reduced(5));
        bandRatio.setBounds(row1.removeFromLeft(knobSize).reduced(5));
        bandAttack.setBounds(row1.removeFromLeft(knobSize).reduced(5));
        bandRelease.setBounds(row1.removeFromLeft(knobSize).reduced(5));
        bandMakeup.setBounds(row1.removeFromLeft(knobSize).reduced(5));

        auto row2 = controlArea.removeFromTop(40);
        bandBypass.setBounds(row2.removeFromLeft(100).reduced(5));
        bandSolo.setBounds(row2.removeFromLeft(100).reduced(5));

        // Global output on right
        globalOutput.setBounds(area.removeFromRight(60).reduced(10));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0d0d0d));

        // Draw frequency bands
        auto specArea = spectrumDisplay.getBounds();
        g.setColour(juce::Colour(0x30ffffff));

        // Draw band divisions
        for (int i = 0; i < 3; ++i)
        {
            float freq = crossoverSliders[i].getValue();
            float x = mapFrequencyToX(freq, specArea);
            g.drawVerticalLine(x, specArea.getY(), specArea.getBottom());
        }

        // Band labels
        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        auto bandWidth = specArea.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            juce::String bandName;
            switch (i)
            {
                case 0: bandName = "LOW"; break;
                case 1: bandName = "LOW-MID"; break;
                case 2: bandName = "HIGH-MID"; break;
                case 3: bandName = "HIGH"; break;
            }
            g.drawText(bandName, specArea.getX() + i * bandWidth, specArea.getY(),
                      bandWidth, 20, juce::Justification::centred);
        }

        // Title
        g.setColour(juce::Colour(0xff00d4ff));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("MULTIBAND COMPRESSOR", 0, 5, getWidth(), 20, juce::Justification::centred);
    }

private:
    juce::AudioProcessorValueTreeState& parameters;

    juce::ComboBox bandSelector;
    std::array<juce::Slider, 3> crossoverSliders;

    juce::Slider bandThreshold, bandRatio, bandAttack, bandRelease, bandMakeup;
    juce::ToggleButton bandBypass, bandSolo;

    juce::Slider globalOutput;
    juce::Component spectrumDisplay;  // Placeholder for spectrum analyzer

    void updateBandControls()
    {
        int band = bandSelector.getSelectedId() - 1;
        // Update controls to show selected band's settings
        // This would load the appropriate parameter values
    }

    float mapFrequencyToX(float freq, juce::Rectangle<int> area)
    {
        // Log scale frequency mapping
        float minLog = std::log10(20.0f);
        float maxLog = std::log10(20000.0f);
        float freqLog = std::log10(freq);
        float normalized = (freqLog - minLog) / (maxLog - minLog);
        return area.getX() + normalized * area.getWidth();
    }
};

//==============================================================================
// Studio VCA Panel (Focusrite Red 3 style)
//==============================================================================
class StudioVCAPanel : public juce::Component
{
public:
    StudioVCAPanel(juce::AudioProcessorValueTreeState& apvts)
        : parameters(apvts)
    {
        // Studio Red 3 color scheme
        setLookAndFeel(&studioLookAndFeel);

        // Threshold control (-40 to +20 dB)
        addAndMakeVisible(thresholdSlider);
        thresholdSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        thresholdSlider.setRange(-40.0, 20.0, 0.1);
        thresholdSlider.setTextValueSuffix(" dB");
        thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Ratio control (1:1 to 10:1)
        addAndMakeVisible(ratioSlider);
        ratioSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        ratioSlider.setRange(1.0, 10.0, 0.1);
        ratioSlider.setTextValueSuffix(":1");
        ratioSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Attack control (0.3 to 75 ms)
        addAndMakeVisible(attackSlider);
        attackSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        attackSlider.setRange(0.3, 75.0, 0.1);
        attackSlider.setSkewFactorFromMidPoint(10.0);
        attackSlider.setTextValueSuffix(" ms");
        attackSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Release control (50 to 3000 ms)
        addAndMakeVisible(releaseSlider);
        releaseSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        releaseSlider.setRange(50.0, 3000.0, 1.0);
        releaseSlider.setSkewFactorFromMidPoint(300.0);
        releaseSlider.setTextValueSuffix(" ms");
        releaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Output/Makeup gain (-20 to +20 dB)
        addAndMakeVisible(outputSlider);
        outputSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        outputSlider.setRange(-20.0, 20.0, 0.1);
        outputSlider.setTextValueSuffix(" dB");
        outputSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Labels
        createLabels();

        // Parameter attachments
        thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "studio_vca_threshold", thresholdSlider);
        ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "studio_vca_ratio", ratioSlider);
        attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "studio_vca_attack", attackSlider);
        releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "studio_vca_release", releaseSlider);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "studio_vca_output", outputSlider);
    }

    ~StudioVCAPanel() override
    {
        setLookAndFeel(nullptr);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);

        // Leave space for title
        area.removeFromTop(30);

        // Main controls row
        auto controlRow = area.removeFromTop(120);
        auto knobWidth = controlRow.getWidth() / 5;

        thresholdSlider.setBounds(controlRow.removeFromLeft(knobWidth).reduced(5));
        ratioSlider.setBounds(controlRow.removeFromLeft(knobWidth).reduced(5));
        attackSlider.setBounds(controlRow.removeFromLeft(knobWidth).reduced(5));
        releaseSlider.setBounds(controlRow.removeFromLeft(knobWidth).reduced(5));
        outputSlider.setBounds(controlRow.removeFromLeft(knobWidth).reduced(5));
    }

    void paint(juce::Graphics& g) override
    {
        // Focusrite Red inspired background
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff2a1518), 0, 0,
            juce::Colour(0xff1a0d0f), 0, getHeight(), false));
        g.fillAll();

        // Subtle red accent line at top
        g.setColour(juce::Colour(0xffcc3333));
        g.fillRect(0, 0, getWidth(), 3);

        // Title with Focusrite Red style
        g.setColour(juce::Colour(0xffcc3333));
        g.setFont(juce::FontOptions(18.0f).withStyle("Bold"));
        g.drawText("STUDIO VCA", 0, 8, getWidth(), 20, juce::Justification::centred);

        // Subtitle
        g.setColour(juce::Colour(0xff888888));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("Focusrite Red 3 Style", 0, 25, getWidth(), 15, juce::Justification::centred);

        // VCA characteristics description
        g.setColour(juce::Colour(0xff666666));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText("RMS Detection | Soft Knee | Clean VCA Dynamics",
                   0, getHeight() - 25, getWidth(), 20, juce::Justification::centred);
    }

private:
    juce::AudioProcessorValueTreeState& parameters;

    // Custom look and feel for Red 3 style
    class StudioVCALookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        StudioVCALookAndFeel()
        {
            // Focusrite Red inspired colors
            setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a0d0f));
            setColour(juce::Slider::thumbColourId, juce::Colour(0xffcc3333));
            setColour(juce::Slider::trackColourId, juce::Colour(0xff993333));
            setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffcc3333));
            setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a1518));
            setColour(juce::Label::textColourId, juce::Colour(0xffd0d0d0));
        }

        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                              juce::Slider&) override
        {
            auto radius = juce::jmin(width / 2, height / 2) - 8.0f;
            auto centreX = x + width * 0.5f;
            auto centreY = y + height * 0.5f;
            auto rx = centreX - radius;
            auto ry = centreY - radius;
            auto rw = radius * 2.0f;
            auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

            // Dark background with subtle gradient
            juce::ColourGradient knobGradient(
                juce::Colour(0xff2a1518), centreX, centreY - radius,
                juce::Colour(0xff1a0d0f), centreX, centreY + radius, false);
            g.setGradientFill(knobGradient);
            g.fillEllipse(rx, ry, rw, rw);

            // Red arc showing value
            juce::Path arc;
            arc.addArc(rx + 3, ry + 3, rw - 6, rw - 6, rotaryStartAngle, angle, true);
            g.setColour(juce::Colour(0xffcc3333));
            g.strokePath(arc, juce::PathStrokeType(3.5f));

            // Outer ring
            g.setColour(juce::Colour(0xff3a2528));
            g.drawEllipse(rx, ry, rw, rw, 1.5f);

            // Center cap
            g.setColour(juce::Colour(0xff2a1518));
            g.fillEllipse(centreX - 8, centreY - 8, 16, 16);

            // Pointer
            juce::Path pointer;
            pointer.startNewSubPath(centreX, centreY);
            pointer.lineTo(centreX + (radius - 12) * std::cos(angle - juce::MathConstants<float>::halfPi),
                          centreY + (radius - 12) * std::sin(angle - juce::MathConstants<float>::halfPi));
            g.setColour(juce::Colour(0xffd0d0d0));
            g.strokePath(pointer, juce::PathStrokeType(2.5f));
        }
    };

    StudioVCALookAndFeel studioLookAndFeel;

    juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider, outputSlider;

    std::vector<std::unique_ptr<juce::Label>> labels;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    void createLabels()
    {
        addLabel("THRESHOLD", thresholdSlider);
        addLabel("RATIO", ratioSlider);
        addLabel("ATTACK", attackSlider);
        addLabel("RELEASE", releaseSlider);
        addLabel("OUTPUT", outputSlider);
    }

    void addLabel(const juce::String& text, juce::Component& component)
    {
        auto label = std::make_unique<juce::Label>(text, text);
        label->attachToComponent(&component, false);
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffa0a0a0));
        label->setFont(juce::FontOptions(11.0f));
        addAndMakeVisible(*label);
        labels.push_back(std::move(label));
    }
};
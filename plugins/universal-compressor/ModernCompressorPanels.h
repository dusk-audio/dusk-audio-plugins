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

    void setScaleFactor(float scale)
    {
        if (scale <= 0.0f)
        {
            jassertfalse; // Invalid scale factor
            return;
        }
        currentScaleFactor = scale;
        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds();

        // ========================================================================
        // Use SAME standardized knob layout constants as main editor
        // ========================================================================
        const int stdLabelHeight = static_cast<int>(22 * currentScaleFactor);
        const int stdKnobSize = static_cast<int>(75 * currentScaleFactor);  // Same as all other modes
        // Use tighter row spacing for 2-row layout
        const int stdKnobRowHeight = stdLabelHeight + stdKnobSize + static_cast<int>(5 * currentScaleFactor);

        // Helper to layout a knob centered in column (matches main editor's layoutKnob)
        auto layoutKnob = [&](juce::Slider& slider, juce::Rectangle<int> colArea) {
            // Labels are attached to sliders via attachToComponent, so leave room at top
            colArea.removeFromTop(stdLabelHeight);
            int knobX = colArea.getX() + (colArea.getWidth() - stdKnobSize) / 2;
            slider.setBounds(knobX, colArea.getY(), stdKnobSize, stdKnobSize);
        };

        // Top row - 5 knobs: Threshold, Ratio, Knee, Attack, Release
        auto topRow = area.removeFromTop(stdKnobRowHeight);
        int knobWidth = topRow.getWidth() / 5;

        layoutKnob(thresholdSlider, topRow.removeFromLeft(knobWidth));
        layoutKnob(ratioSlider, topRow.removeFromLeft(knobWidth));
        layoutKnob(kneeSlider, topRow.removeFromLeft(knobWidth));
        layoutKnob(attackSlider, topRow.removeFromLeft(knobWidth));
        layoutKnob(releaseSlider, topRow);

        // Bottom row - 5 columns: Lookahead, Mix, Output + 2 buttons
        auto bottomRow = area.removeFromTop(stdKnobRowHeight);
        knobWidth = bottomRow.getWidth() / 5;

        layoutKnob(lookaheadSlider, bottomRow.removeFromLeft(knobWidth));
        layoutKnob(mixSlider, bottomRow.removeFromLeft(knobWidth));
        layoutKnob(outputSlider, bottomRow.removeFromLeft(knobWidth));

        // Place buttons in remaining 2 columns, vertically centered
        int buttonHeight = static_cast<int>(24 * currentScaleFactor);
        int buttonY = bottomRow.getY() + stdLabelHeight + (stdKnobSize - buttonHeight) / 2;

        auto buttonCol1 = bottomRow.removeFromLeft(knobWidth);
        adaptiveReleaseButton.setBounds(buttonCol1.getX() + 5, buttonY,
                                        buttonCol1.getWidth() - 10, buttonHeight);

        auto buttonCol2 = bottomRow;
        sidechainListenButton.setBounds(buttonCol2.getX() + 5, buttonY,
                                        buttonCol2.getWidth() - 10, buttonHeight);

        // Hide sidechain EQ button for now (not implemented)
        sidechainEQButton.setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        // Background and title are handled by parent editor - nothing to draw here
        juce::ignoreUnused(g);
    }

private:
    juce::AudioProcessorValueTreeState& parameters;
    float currentScaleFactor = 1.0f;

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
        // Note: Look and feel is now set externally by the editor for consistency

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
        // Look and feel is managed externally
    }

    void setAutoGainEnabled(bool enabled)
    {
        const float disabledAlpha = 0.4f;
        const float enabledAlpha = 1.0f;
        outputSlider.setEnabled(!enabled);
        outputSlider.setAlpha(enabled ? disabledAlpha : enabledAlpha);
    }

    void setScaleFactor(float scale)
    {
        currentScaleFactor = scale;
        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(static_cast<int>(5 * currentScaleFactor));

        // Leave space for title at top (compact)
        area.removeFromTop(static_cast<int>(25 * currentScaleFactor));

        // Leave space for bottom description
        area.removeFromBottom(static_cast<int>(20 * currentScaleFactor));

        // Standardized knob size matching other compressor modes - SCALED
        const int stdKnobSize = static_cast<int>(75 * currentScaleFactor);
        const int stdLabelHeight = static_cast<int>(22 * currentScaleFactor);
        const int stdKnobRowHeight = stdLabelHeight + stdKnobSize + static_cast<int>(10 * currentScaleFactor);

        // Center the row vertically in available space
        auto controlRow = area.withHeight(stdKnobRowHeight);
        controlRow.setY(area.getY() + (area.getHeight() - stdKnobRowHeight) / 2);

        auto knobWidth = controlRow.getWidth() / 5;

        // Helper to layout a knob with label above (matching main editor's layoutKnob lambda)
        auto layoutKnob = [&](juce::Slider& slider, juce::Rectangle<int> colArea) {
            // Labels are attached to sliders, so skip the label height
            colArea.removeFromTop(stdLabelHeight);
            int knobX = colArea.getX() + (colArea.getWidth() - stdKnobSize) / 2;
            slider.setBounds(knobX, colArea.getY(), stdKnobSize, stdKnobSize);
        };

        layoutKnob(thresholdSlider, controlRow.removeFromLeft(knobWidth));
        layoutKnob(ratioSlider, controlRow.removeFromLeft(knobWidth));
        layoutKnob(attackSlider, controlRow.removeFromLeft(knobWidth));
        layoutKnob(releaseSlider, controlRow.removeFromLeft(knobWidth));
        layoutKnob(outputSlider, controlRow);
    }

    void paint(juce::Graphics& g) override
    {
        // Dark red inspired background
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff2a1518), 0, 0,
            juce::Colour(0xff1a0d0f), 0, getHeight(), false));
        g.fillAll();

        // Red accent line at very top
        g.setColour(juce::Colour(0xffcc3333));
        g.fillRect(0, 0, getWidth(), 2);

        // Title - right below the red line
        g.setColour(juce::Colour(0xffcc3333));
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText("STUDIO VCA", 0, 3, getWidth(), 16, juce::Justification::centred);

        // VCA characteristics description at bottom
        g.setColour(juce::Colour(0xff666666));
        g.setFont(juce::Font(10.0f));
        g.drawText("RMS Detection | Soft Knee | Clean VCA Dynamics",
                   0, getHeight() - 18, getWidth(), 16, juce::Justification::centred);
    }

private:
    juce::AudioProcessorValueTreeState& parameters;
    float currentScaleFactor = 1.0f;

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
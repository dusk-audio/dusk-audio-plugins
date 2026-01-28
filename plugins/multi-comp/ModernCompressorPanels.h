/*
  ==============================================================================

    ModernCompressorPanels.h - UI for Digital and Multiband compressor modes

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include "UniversalCompressor.h"
#include "../shared/LunaLookAndFeel.h"

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
        LunaSliderStyle::configureKnob(thresholdSlider);
        thresholdSlider.setRange(-60.0, 0.0, 0.1);
        thresholdSlider.setTextValueSuffix(" dB");
        thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        addAndMakeVisible(ratioSlider);
        ratioSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(ratioSlider);
        ratioSlider.setRange(1.0, 100.0, 0.1);
        ratioSlider.setSkewFactorFromMidPoint(10.0);
        ratioSlider.setTextValueSuffix(":1");
        ratioSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        addAndMakeVisible(kneeSlider);
        kneeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(kneeSlider);
        kneeSlider.setRange(0.0, 20.0, 0.1);
        kneeSlider.setTextValueSuffix(" dB");
        kneeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Time controls
        addAndMakeVisible(attackSlider);
        attackSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(attackSlider);
        attackSlider.setRange(0.01, 500.0, 0.01);
        attackSlider.setSkewFactorFromMidPoint(5.0);
        attackSlider.setTextValueSuffix(" ms");
        attackSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        addAndMakeVisible(releaseSlider);
        releaseSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(releaseSlider);
        releaseSlider.setRange(1.0, 5000.0, 1.0);
        releaseSlider.setSkewFactorFromMidPoint(500.0);
        releaseSlider.setTextValueSuffix(" ms");
        releaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Lookahead
        addAndMakeVisible(lookaheadSlider);
        lookaheadSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(lookaheadSlider);
        lookaheadSlider.setRange(0.0, 10.0, 0.1);
        lookaheadSlider.setTextValueSuffix(" ms");
        lookaheadSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Mix control
        addAndMakeVisible(mixSlider);
        mixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(mixSlider);
        mixSlider.setRange(0.0, 100.0, 1.0);
        mixSlider.setTextValueSuffix(" %");
        mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Output
        addAndMakeVisible(outputSlider);
        outputSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(outputSlider);
        outputSlider.setRange(-24.0, 24.0, 0.1);
        outputSlider.setTextValueSuffix(" dB");
        outputSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Advanced features
        addAndMakeVisible(adaptiveReleaseButton);
        adaptiveReleaseButton.setButtonText("Adaptive Release");

        // SC Listen is now a global control in the header for all modes

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
        // Use global mix parameter for consistency across all modes
        mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mix", mixSlider);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "digital_output", outputSlider);
        adaptiveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            parameters, "digital_adaptive", adaptiveReleaseButton);
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

    void setAutoGainEnabled(bool enabled)
    {
        const float disabledAlpha = 0.4f;
        const float enabledAlpha = 1.0f;
        outputSlider.setEnabled(!enabled);
        outputSlider.setAlpha(enabled ? disabledAlpha : enabledAlpha);
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

    juce::ToggleButton adaptiveReleaseButton;
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
// Crossover Fader Component - A vertical fader positioned between bands
// Drag up to increase frequency, drag down to decrease
//==============================================================================
class CrossoverFader : public juce::Component
{
public:
    CrossoverFader(juce::Slider& slider, int index, juce::Colour leftColor, juce::Colour rightColor)
        : linkedSlider(slider), handleIndex(index), leftBandColor(leftColor), rightBandColor(rightColor)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        setOpaque(false);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float centreX = bounds.getCentreX();

        // Fader track area - aligned with meter bounds
        float trackWidth = 8.0f;
        float trackX = centreX - trackWidth / 2;
        float trackTop = bounds.getY();  // Flush with top of meters
        float trackBottom = bounds.getBottom() - 22.0f;  // Leave room for frequency label below meters
        float trackHeight = trackBottom - trackTop;

        // Draw vertical track background
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillRoundedRectangle(trackX - 2, trackTop, trackWidth + 4, trackHeight, 4.0f);

        // Track gradient showing frequency direction
        juce::ColourGradient trackGrad(
            rightBandColor.withAlpha(0.3f), trackX, trackTop,
            leftBandColor.withAlpha(0.3f), trackX, trackBottom, false);
        g.setGradientFill(trackGrad);
        g.fillRoundedRectangle(trackX, trackTop + 2, trackWidth, trackHeight - 4, 3.0f);

        // Calculate thumb position based on frequency (logarithmic scale)
        double freq = linkedSlider.getValue();
        double minFreq = juce::jmax(0.1, linkedSlider.getMinimum());
        double maxFreq = juce::jmax(minFreq + 0.1, linkedSlider.getMaximum());
        double clampedFreq = juce::jlimit(minFreq, maxFreq, freq);
        double logMin = std::log(minFreq);
        double logMax = std::log(maxFreq);
        double logFreq = std::log(clampedFreq);
        float normalizedPos = static_cast<float>((logFreq - logMin) / (logMax - logMin));

        // Invert: higher frequency = higher position (lower Y)
        float thumbY = trackBottom - normalizedPos * trackHeight;        // Draw thumb/handle
        float thumbWidth = 26.0f;
        float thumbHeight = 18.0f;
        float thumbX = centreX - thumbWidth / 2;
        float thumbYCentered = thumbY - thumbHeight / 2;

        // Thumb shadow
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(thumbX + 1, thumbYCentered + 2, thumbWidth, thumbHeight, 4.0f);

        // Thumb background with gradient
        juce::Colour thumbColor = (isHovered || isDragging) ? juce::Colour(0xff00d4ff) : juce::Colour(0xff404050);
        juce::ColourGradient thumbGrad(
            thumbColor.brighter(0.3f), thumbX, thumbYCentered,
            thumbColor.darker(0.2f), thumbX, thumbYCentered + thumbHeight, false);
        g.setGradientFill(thumbGrad);
        g.fillRoundedRectangle(thumbX, thumbYCentered, thumbWidth, thumbHeight, 4.0f);

        // Thumb grip lines
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        for (int i = -1; i <= 1; ++i)
        {
            float lineY = thumbYCentered + thumbHeight / 2 + i * 4.0f;
            g.drawHorizontalLine(static_cast<int>(lineY), thumbX + 6, thumbX + thumbWidth - 6);
        }

        // Thumb border
        g.setColour((isHovered || isDragging) ? juce::Colour(0xff00d4ff) : juce::Colours::white.withAlpha(0.3f));
        g.drawRoundedRectangle(thumbX, thumbYCentered, thumbWidth, thumbHeight, 4.0f, 1.5f);

        // Draw arrows on either side of track to indicate drag direction
        if (isHovered || isDragging)
        {
            g.setColour(juce::Colour(0xff00d4ff).withAlpha(0.8f));

            // Up arrow (higher freq)
            juce::Path upArrow;
            upArrow.addTriangle(centreX, trackTop - 2, centreX - 5, trackTop + 5, centreX + 5, trackTop + 5);
            g.fillPath(upArrow);

            // Down arrow (lower freq)
            juce::Path downArrow;
            downArrow.addTriangle(centreX, trackBottom + 2, centreX - 5, trackBottom - 5, centreX + 5, trackBottom - 5);
            g.fillPath(downArrow);
        }

        // Frequency label at the bottom of the component (below the GR meter values)
        g.setColour((isHovered || isDragging) ? juce::Colour(0xff00d4ff) : juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        juce::String freqText = formatFrequency(static_cast<float>(freq));
        g.drawText(freqText, 0, static_cast<int>(bounds.getBottom() - 18), static_cast<int>(bounds.getWidth()), 16,
                   juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        isHovered = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        isHovered = false;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        isDragging = true;
        lastMouseY = e.y;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (isDragging)
        {
            // Calculate track dimensions
            auto bounds = getLocalBounds().toFloat();
            float trackTop = bounds.getY() + 4.0f;
            float trackBottom = bounds.getBottom() - 22.0f;
            float trackHeight = trackBottom - trackTop;
            // Calculate delta in pixels
            int deltaY = lastMouseY - e.y;  // Positive when dragging up

            if (deltaY != 0)
            {
                // Use logarithmic scaling for frequency changes
                double logMin = std::log(linkedSlider.getMinimum());
                double logMax = std::log(linkedSlider.getMaximum());
                double logRange = logMax - logMin;

                // Convert pixel delta to log frequency delta
                // Dragging up increases frequency, dragging down decreases
                double sensitivity = logRange / trackHeight;
                double logDelta = deltaY * sensitivity;

                double currentLogValue = std::log(linkedSlider.getValue());
                double newLogValue = juce::jlimit(logMin, logMax, currentLogValue + logDelta);
                double newValue = std::exp(newLogValue);

                linkedSlider.setValue(newValue);
                lastMouseY = e.y;

                if (auto* parent = getParentComponent())
                    parent->repaint();
            }
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        isDragging = false;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        // Reset to default value on double-click
        linkedSlider.setValue(linkedSlider.getDoubleClickReturnValue());
        repaint();
    }

    bool isDraggingHandle() const { return isDragging; }
    double getCurrentValue() const { return linkedSlider.getValue(); }
    int getHandleIndex() const { return handleIndex; }

private:
    juce::Slider& linkedSlider;
    int handleIndex;
    juce::Colour leftBandColor;
    juce::Colour rightBandColor;
    bool isHovered = false;
    bool isDragging = false;
    int lastMouseY = 0;

    static juce::String formatFrequency(float freq)
    {
        if (freq >= 1000.0f)
            return juce::String(freq / 1000.0f, 1) + " kHz";
        else
            return juce::String(static_cast<int>(freq)) + " Hz";
    }
};

//==============================================================================
// Multiband Compressor Panel - Modern digital design inspired by FabFilter Pro-MB
//==============================================================================
class MultibandCompressorPanel : public juce::Component, private juce::Timer
{
public:
    // Band colors (matching modern multiband compressor aesthetics)
    static constexpr juce::uint32 bandColors[4] = {
        0xffff6b6b,  // Low - Red/coral
        0xffffd93d,  // Low-Mid - Yellow/gold
        0xff6bcb77,  // High-Mid - Green
        0xff4d96ff   // High - Blue
    };

    MultibandCompressorPanel(juce::AudioProcessorValueTreeState& apvts)
        : parameters(apvts)
    {
        // Band selector buttons with hover effect
        for (int i = 0; i < 4; ++i)
        {
            bandButtons[i].setButtonText(getBandName(i));
            bandButtons[i].setClickingTogglesState(true);
            bandButtons[i].setRadioGroupId(1001);
            bandButtons[i].setColour(juce::TextButton::buttonColourId, juce::Colour(bandColors[i]).withAlpha(0.3f));
            bandButtons[i].setColour(juce::TextButton::buttonOnColourId, juce::Colour(bandColors[i]));
            bandButtons[i].onClick = [this, i] { selectBand(i); };
            addAndMakeVisible(bandButtons[i]);

            // Per-band solo button (S) - improved visibility with illuminated state
            bandSoloButtons[i].setButtonText("S");
            bandSoloButtons[i].setClickingTogglesState(true);
            bandSoloButtons[i].setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
            bandSoloButtons[i].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffcc00));
            bandSoloButtons[i].setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaaaaa));
            bandSoloButtons[i].setColour(juce::TextButton::textColourOnId, juce::Colours::black);
            bandSoloButtons[i].setTooltip("Solo " + getBandName(i) + " band");
            addAndMakeVisible(bandSoloButtons[i]);
        }
        bandButtons[0].setToggleState(true, juce::dontSendNotification);

        // Hidden crossover sliders (we use custom faders for interaction)
        for (int i = 0; i < 3; ++i)
        {
            crossoverSliders[i].setSliderStyle(juce::Slider::LinearHorizontal);
            crossoverSliders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            crossoverSliders[i].setVisible(false);  // Hidden - we draw custom faders
            addChildComponent(crossoverSliders[i]);

            // Create custom crossover fader between bands i and i+1
            juce::Colour leftColor = juce::Colour(bandColors[i]);
            juce::Colour rightColor = juce::Colour(bandColors[i + 1]);
            crossoverFaders[i] = std::make_unique<CrossoverFader>(crossoverSliders[i], i, leftColor, rightColor);
            addAndMakeVisible(crossoverFaders[i].get());
        }

        // Create crossover attachments
        crossoverAttachments[0] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mb_crossover_1", crossoverSliders[0]);
        crossoverAttachments[1] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mb_crossover_2", crossoverSliders[1]);
        crossoverAttachments[2] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mb_crossover_3", crossoverSliders[2]);

        // Per-band controls (5 knobs) with double-click to reset
        auto setupKnob = [this](juce::Slider& slider, const juce::String& suffix, double defaultVal) {
            slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            LunaSliderStyle::configureKnob(slider);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
            if (suffix.isNotEmpty())
                slider.setTextValueSuffix(suffix);
            slider.setDoubleClickReturnValue(true, defaultVal);
            slider.setPopupDisplayEnabled(true, true, this);
            addAndMakeVisible(slider);
        };

        setupKnob(bandThreshold, " dB", -20.0);
        setupKnob(bandRatio, ":1", 4.0);
        setupKnob(bandAttack, " ms", 10.0);
        setupKnob(bandRelease, " ms", 100.0);
        setupKnob(bandMakeup, " dB", 0.0);

        // Per-band solo attachments
        const juce::String bandNames[] = {"low", "lowmid", "highmid", "high"};
        for (int i = 0; i < 4; ++i)
        {
            bandSoloAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                parameters, "mb_" + bandNames[i] + "_solo", bandSoloButtons[i]);
        }

        // Global controls
        setupKnob(globalOutput, " dB", 0.0);
        setupKnob(globalMix, "%", 100.0);

        // Global output and mix attachments
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mb_output", globalOutput);
        // Use global mix parameter for consistency across all modes
        mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mix", globalMix);

        // Labels
        const char* labelTexts[] = {"Threshold", "Ratio", "Attack", "Release", "Makeup", "Output", "Mix"};
        for (int i = 0; i < 7; ++i)
        {
            knobLabels[i].setText(labelTexts[i], juce::dontSendNotification);
            knobLabels[i].setJustificationType(juce::Justification::centred);
            knobLabels[i].setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
            knobLabels[i].setFont(juce::FontOptions(11.0f));
            addAndMakeVisible(knobLabels[i]);
        }

        // Initialize with first band
        selectBand(0);

        // Start timer for crossover label updates
        startTimerHz(30);
    }

    ~MultibandCompressorPanel() override
    {
        stopTimer();
    }

    void setScaleFactor(float scale)
    {
        if (std::isnan(scale) || scale <= 0.0f)
        {
            jassertfalse;
            return;
        }
        scaleFactor = scale;
        resized();
    }

    void setAutoGainEnabled(bool enabled)
    {
        const float alpha = enabled ? 0.4f : 1.0f;
        globalOutput.setEnabled(!enabled);
        globalOutput.setAlpha(alpha);
    }

    // Called from timer to update GR meters with smooth ballistics
    void setBandGainReduction(int band, float grDb)
    {
        if (band >= 0 && band < 4)
        {
            // Smooth ballistics - fast attack, slower release
            float targetGR = grDb;
            float currentGR = smoothedBandGR[band];

            if (targetGR < currentGR)
            {
                // Attack - fast
                smoothedBandGR[band] = currentGR + (targetGR - currentGR) * 0.5f;
            }
            else
            {
                // Release - slower
                smoothedBandGR[band] = currentGR + (targetGR - currentGR) * 0.15f;
            }

            // Update peak hold
            if (-smoothedBandGR[band] > -peakHoldGR[band])
            {
                peakHoldGR[band] = smoothedBandGR[band];
                peakHoldTime[band] = 30;  // Hold for ~1 second at 30fps
            }
            else if (peakHoldTime[band] > 0)
            {
                peakHoldTime[band]--;
            }
            else
            {
                // Decay peak hold
                peakHoldGR[band] = peakHoldGR[band] * 0.95f;
            }

            bandGR[band] = smoothedBandGR[band];
            repaint();
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int margin = static_cast<int>(5 * scaleFactor);

        // === BOTTOM SECTION: Knobs (reserve space first) ===
        const int knobSize = static_cast<int>(55 * scaleFactor);
        const int labelHeight = static_cast<int>(16 * scaleFactor);
        const int knobSectionHeight = labelHeight + knobSize + static_cast<int>(25 * scaleFactor);
        auto knobSection = area.removeFromBottom(knobSectionHeight);

        // Reserve space for the band indicator at the bottom
        area.removeFromBottom(static_cast<int>(28 * scaleFactor));

        // === TOP SECTION: Band visualization with GR meters ===
        auto topSection = area;

        // Reserve space for dB scale on left side
        const int scaleWidth = static_cast<int>(30 * scaleFactor);
        dbScaleBounds = topSection.removeFromLeft(scaleWidth);

        topSection.reduce(margin, margin);

        // Add "GAIN REDUCTION" label area at top
        topSection.removeFromTop(static_cast<int>(18 * scaleFactor));
        topSection.removeFromTop(static_cast<int>(4 * scaleFactor));

        // Store meter section bounds for drawing
        meterSectionBounds = topSection;

        // Calculate band widths
        int bandWidth = topSection.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto bandArea = topSection.withX(topSection.getX() + i * bandWidth).withWidth(bandWidth).reduced(3, 0);

            // Calculate meter bounds first so we can align buttons to it
            auto meterArea = bandArea;
            meterArea.removeFromTop(static_cast<int>(32 * scaleFactor));  // Space for buttons + gap
            meterArea.removeFromBottom(static_cast<int>(22 * scaleFactor));  // Space for freq label
            auto meterBounds = meterArea.reduced(static_cast<int>(4 * scaleFactor), static_cast<int>(2 * scaleFactor));

            // Top row: Band button and Solo button side by side, centered over meter
            int buttonRowHeight = static_cast<int>(26 * scaleFactor);
            int soloWidth = static_cast<int>(24 * scaleFactor);
            int buttonGap = static_cast<int>(3 * scaleFactor);
            int totalButtonWidth = meterBounds.getWidth();  // Match meter width
            int minBandButtonWidth = static_cast<int>(30 * scaleFactor);
            int requiredMinWidth = minBandButtonWidth + soloWidth + buttonGap;

            // Handle narrow meter edge case - allow smaller buttons if necessary
            int bandButtonWidth = (totalButtonWidth >= requiredMinWidth)
                ? totalButtonWidth - soloWidth - buttonGap
                : juce::jmax(static_cast<int>(20 * scaleFactor), totalButtonWidth - soloWidth - buttonGap);

            // Center buttons over the meter
            int buttonsX = meterBounds.getX();
            int buttonsY = bandArea.getY();

            bandButtons[i].setBounds(buttonsX, buttonsY, bandButtonWidth, buttonRowHeight);
            bandSoloButtons[i].setBounds(buttonsX + bandButtonWidth + buttonGap, buttonsY, soloWidth, buttonRowHeight);

            // Store meter bounds
            bandGRBounds[i] = meterBounds;
        }

        // Store meter section info for handle positioning
        // Use the actual meter bounds to align faders with meters
        crossoverAreaLeft = topSection.getX();
        crossoverAreaWidth = topSection.getWidth();
        // Align fader top with the top of the GR meters (after buttons + gap)
        crossoverHandleTop = bandGRBounds[0].getY();
        crossoverHandleHeight = bandGRBounds[0].getHeight();

        // Position crossover faders between bands
        updateCrossoverFaderPositions();

        // === Layout knobs ===
        const int numKnobs = 7;
        const int colWidth = knobSection.getWidth() / numKnobs;

        auto layoutKnob = [&](juce::Slider& slider, juce::Label& label, int col) {
            auto colArea = knobSection.withX(knobSection.getX() + col * colWidth).withWidth(colWidth);
            label.setBounds(colArea.removeFromTop(labelHeight));
            int knobX = colArea.getX() + (colArea.getWidth() - knobSize) / 2;
            int knobY = colArea.getY() + 2;
            slider.setBounds(knobX, knobY, knobSize, knobSize);
        };

        layoutKnob(bandThreshold, knobLabels[0], 0);
        layoutKnob(bandRatio, knobLabels[1], 1);
        layoutKnob(bandAttack, knobLabels[2], 2);
        layoutKnob(bandRelease, knobLabels[3], 3);
        layoutKnob(bandMakeup, knobLabels[4], 4);
        layoutKnob(globalOutput, knobLabels[5], 5);
        layoutKnob(globalMix, knobLabels[6], 6);
    }

    void paint(juce::Graphics& g) override
    {
        // Background with subtle gradient
        juce::ColourGradient bgGradient(juce::Colour(0xff1a1a1e), 0, 0,
                                         juce::Colour(0xff141418), 0, static_cast<float>(getHeight()), false);
        g.setGradientFill(bgGradient);
        g.fillAll();

        // Draw "GAIN REDUCTION" label
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.setFont(juce::FontOptions(11.0f * scaleFactor).withStyle("Bold"));
        g.drawText("GAIN REDUCTION", meterSectionBounds.getX(), meterSectionBounds.getY() - static_cast<int>(20 * scaleFactor),
                   meterSectionBounds.getWidth(), static_cast<int>(16 * scaleFactor), juce::Justification::centred);

        // Draw dB scale on left side
        drawDbScale(g);

        // Draw band backgrounds (colored tint for each band region)
        drawBandBackgrounds(g);

        // Draw band GR meters with LED-style segments
        for (int i = 0; i < 4; ++i)
        {
            drawBandMeter(g, i);
        }

        // Note: Crossover faders now draw their own frequency labels

        // Draw selected band indicator - tab style with color underline
        drawBandIndicator(g);
    }

    void timerCallback() override
    {
        // Update crossover fader positions whenever values change
        updateCrossoverFaderPositions();

        // Repaint if any fader is being dragged
        bool anyDragging = false;
        for (int i = 0; i < 3; ++i)
        {
            if (crossoverFaders[i] && crossoverFaders[i]->isDraggingHandle())
            {
                anyDragging = true;
                break;
            }
        }
        if (anyDragging)
            repaint();
    }

private:
    juce::AudioProcessorValueTreeState& parameters;
    float scaleFactor = 1.0f;
    int currentBand = 0;
    float bandGR[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float smoothedBandGR[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float peakHoldGR[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int peakHoldTime[4] = {0, 0, 0, 0};
    juce::Rectangle<int> bandGRBounds[4];
    juce::Rectangle<int> dbScaleBounds;
    juce::Rectangle<int> meterSectionBounds;

    // Crossover handle positioning info
    int crossoverAreaLeft = 0;
    int crossoverAreaWidth = 0;
    int crossoverHandleTop = 0;
    int crossoverHandleHeight = 0;

    // Frequency range for logarithmic mapping (20 Hz to 20 kHz)
    static constexpr double minFreq = 20.0;
    static constexpr double maxFreq = 20000.0;

    // Band selection buttons
    std::array<juce::TextButton, 4> bandButtons;

    // Crossover sliders (hidden, used for parameter attachment)
    std::array<juce::Slider, 3> crossoverSliders;
    std::array<std::unique_ptr<CrossoverFader>, 3> crossoverFaders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> crossoverAttachments;

    // Per-band controls
    juce::Slider bandThreshold, bandRatio, bandAttack, bandRelease, bandMakeup;
    std::array<juce::TextButton, 4> bandSoloButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 4> bandSoloAttachments;
    std::array<juce::Label, 7> knobLabels;

    // Per-band parameter attachments (recreated when band changes)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> makeupAttachment;

    // Global controls
    juce::Slider globalOutput, globalMix;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    // Convert frequency to normalized position (0-1) using logarithmic scale
    float frequencyToNormalizedPosition(double freq) const
    {
        double logMin = std::log(minFreq);
        double logMax = std::log(maxFreq);
        double logFreq = std::log(juce::jlimit(minFreq, maxFreq, freq));
        return static_cast<float>((logFreq - logMin) / (logMax - logMin));
    }

    // Convert normalized position (0-1) to pixel X coordinate
    int normalizedPositionToPixelX(float normalized) const
    {
        return crossoverAreaLeft + static_cast<int>(normalized * crossoverAreaWidth);
    }

    // Update crossover fader positions - placed between bands
    void updateCrossoverFaderPositions()
    {
        if (crossoverAreaWidth <= 0) return;

        // Faders are positioned between the band meters
        int bandWidth = meterSectionBounds.getWidth() / 4;
        int faderWidth = static_cast<int>(40 * scaleFactor);
        // Extend fader height to include space for frequency label below meters
        int faderHeight = crossoverHandleHeight + static_cast<int>(22 * scaleFactor);

        for (int i = 0; i < 3; ++i)
        {
            if (crossoverFaders[i])
            {
                // Position at the boundary between band i and band i+1
                int boundaryX = meterSectionBounds.getX() + (i + 1) * bandWidth;

                crossoverFaders[i]->setBounds(
                    boundaryX - faderWidth / 2,
                    crossoverHandleTop,
                    faderWidth,
                    faderHeight
                );
            }
        }
    }

    void drawDbScale(juce::Graphics& g)
    {
        if (bandGRBounds[0].isEmpty()) return;

        auto& refBounds = bandGRBounds[0];
        const float dbLevels[] = {0.0f, -3.0f, -6.0f, -10.0f, -15.0f, -20.0f};
        const int meterTop = refBounds.getY();
        const int meterHeight = refBounds.getHeight();

        g.setFont(juce::FontOptions(9.0f * scaleFactor));

        for (float db : dbLevels)
        {
            float normalized = -db / 20.0f;
            int yPos = meterTop + static_cast<int>(normalized * meterHeight);

            // Tick mark
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.fillRect(dbScaleBounds.getRight() - 6, yPos, 6, 1);

            // Label
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            juce::String label = (db == 0.0f) ? "0" : juce::String(static_cast<int>(db));
            g.drawText(label, dbScaleBounds.getX(), yPos - 6,
                      dbScaleBounds.getWidth() - 8, 12, juce::Justification::centredRight);
        }
    }

    void drawBandBackgrounds(juce::Graphics& g)
    {
        // Draw subtle colored backgrounds for each band
        if (bandGRBounds[0].isEmpty()) return;

        int bandWidth = meterSectionBounds.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto bandArea = meterSectionBounds.withX(meterSectionBounds.getX() + i * bandWidth)
                                              .withWidth(bandWidth);

            juce::Colour bandCol = juce::Colour(bandColors[i]);
            bool isSelected = (currentBand == i);

            // Very subtle band tint
            g.setColour(bandCol.withAlpha(isSelected ? 0.08f : 0.03f));
            g.fillRect(bandArea.reduced(1, 0));
        }
    }

    void drawBandMeter(juce::Graphics& g, int bandIndex)
    {
        auto& bounds = bandGRBounds[bandIndex];
        if (bounds.isEmpty()) return;

        juce::Colour bandCol = juce::Colour(bandColors[bandIndex]);
        bool isSelected = (currentBand == bandIndex);

        // Outer glow for selected band
        if (isSelected)
        {
            g.setColour(bandCol.withAlpha(0.2f));
            g.fillRoundedRectangle(bounds.toFloat().expanded(4), 6.0f);
        }

        // Meter background with gradient
        juce::ColourGradient bgGrad(juce::Colour(0xff0c0c0c), bounds.getX(), bounds.getY(),
                                     juce::Colour(0xff181818), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(bounds.toFloat(), 5.0f);

        // Inner shadow
        g.setColour(juce::Colour(0xff000000).withAlpha(0.5f));
        g.drawRoundedRectangle(bounds.toFloat().reduced(1), 4.0f, 1.0f);

        // Draw LED-style segments
        auto meterBounds = bounds.reduced(4);
        const int numSegments = 20;
        const float segmentGap = 2.0f * scaleFactor;
        float segmentHeight = (meterBounds.getHeight() - (numSegments - 1) * segmentGap) / numSegments;

        float grNormalized = juce::jlimit(0.0f, 1.0f, -bandGR[bandIndex] / 20.0f);
        int litSegments = static_cast<int>(grNormalized * numSegments);

        // Peak hold segment
        float peakNormalized = juce::jlimit(0.0f, 1.0f, -peakHoldGR[bandIndex] / 20.0f);
        int peakSegment = static_cast<int>(peakNormalized * numSegments);

        for (int seg = 0; seg < numSegments; ++seg)
        {
            float y = meterBounds.getY() + seg * (segmentHeight + segmentGap);
            auto segRect = juce::Rectangle<float>(meterBounds.getX(), y,
                                                   meterBounds.getWidth(), segmentHeight);

            bool isLit = seg < litSegments;
            bool isPeakHold = seg == peakSegment && peakSegment > 0;

            if (isLit)
            {
                // Color gradient based on intensity: band color -> yellow -> orange -> red
                float intensity = static_cast<float>(seg) / numSegments;
                juce::Colour segColor;
                if (intensity < 0.4f)
                    segColor = bandCol.brighter(0.2f);
                else if (intensity < 0.6f)
                    segColor = bandCol.interpolatedWith(juce::Colour(0xffffdd00), (intensity - 0.4f) * 5.0f);
                else if (intensity < 0.8f)
                    segColor = juce::Colour(0xffffdd00).interpolatedWith(juce::Colour(0xffff8800), (intensity - 0.6f) * 5.0f);
                else
                    segColor = juce::Colour(0xffff8800).interpolatedWith(juce::Colour(0xffff3333), (intensity - 0.8f) * 5.0f);

                // Bright gradient for 3D LED effect
                juce::ColourGradient segGrad(segColor.brighter(0.4f), segRect.getX(), segRect.getY(),
                                              segColor.darker(0.1f), segRect.getX(), segRect.getBottom(), false);
                g.setGradientFill(segGrad);
                g.fillRoundedRectangle(segRect, 2.0f);

                // Glossy highlight
                g.setColour(segColor.brighter(0.7f).withAlpha(0.4f));
                g.fillRect(segRect.getX() + 2, segRect.getY() + 1, segRect.getWidth() - 4, 1.5f);
            }
            else if (isPeakHold)
            {
                // Peak hold indicator - bright white with glow
                g.setColour(juce::Colours::white.withAlpha(0.3f));
                g.fillRoundedRectangle(segRect.expanded(1), 3.0f);
                g.setColour(juce::Colours::white);
                g.fillRoundedRectangle(segRect, 2.0f);
            }
            else
            {
                // Unlit segment - subtle dark with slight bevel
                g.setColour(juce::Colour(0xff1c1c1c));
                g.fillRoundedRectangle(segRect, 2.0f);
                g.setColour(juce::Colour(0xff252525));
                g.fillRect(segRect.getX() + 2, segRect.getY() + 1, segRect.getWidth() - 4, 1.0f);
            }
        }

        // Border - brighter and thicker when selected
        g.setColour(bandCol.withAlpha(isSelected ? 1.0f : 0.5f));
        g.drawRoundedRectangle(bounds.toFloat(), 5.0f, isSelected ? 2.5f : 1.5f);

        // GR value display at bottom of meter
        g.setFont(juce::FontOptions(12.0f * scaleFactor).withStyle("Bold"));

        // Background pill for value with gradient
        auto valueBounds = bounds.withHeight(static_cast<int>(20 * scaleFactor))
                                  .withY(bounds.getBottom() - static_cast<int>(24 * scaleFactor))
                                  .reduced(6, 0);

        juce::ColourGradient pillGrad(juce::Colour(0xff0a0a0a), valueBounds.getX(), valueBounds.getY(),
                                       juce::Colour(0xff1a1a1a), valueBounds.getX(), valueBounds.getBottom(), false);
        g.setGradientFill(pillGrad);
        g.fillRoundedRectangle(valueBounds.toFloat(), 4.0f);

        g.setColour(bandCol.withAlpha(0.5f));
        g.drawRoundedRectangle(valueBounds.toFloat(), 4.0f, 1.0f);

        // Value text with shadow
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawText(juce::String(bandGR[bandIndex], 1) + " dB",
                   valueBounds.translated(1, 1), juce::Justification::centred);
        g.setColour(juce::Colours::white);
        g.drawText(juce::String(bandGR[bandIndex], 1) + " dB", valueBounds, juce::Justification::centred);
    }

    void drawBandIndicator(juce::Graphics& g)
    {
        juce::Colour bandCol = juce::Colour(bandColors[currentBand]);

        auto labelHeight = static_cast<int>(26 * scaleFactor);
        auto labelY = getHeight() - labelHeight - static_cast<int>(3 * scaleFactor);
        auto labelWidth = static_cast<int>(140 * scaleFactor);
        auto labelX = getWidth() / 2 - labelWidth / 2;
        auto cornerRadius = 5.0f * scaleFactor;

        // Drop shadow
        g.setColour(juce::Colour(0xff000000).withAlpha(0.4f));
        g.fillRoundedRectangle(static_cast<float>(labelX + 2), static_cast<float>(labelY + 2),
                               static_cast<float>(labelWidth), static_cast<float>(labelHeight), cornerRadius);

        // Background with gradient
        juce::ColourGradient bgGrad(juce::Colour(0xff2e2e2e), labelX, labelY,
                                     juce::Colour(0xff222222), labelX, labelY + labelHeight, false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(static_cast<float>(labelX), static_cast<float>(labelY),
                               static_cast<float>(labelWidth), static_cast<float>(labelHeight), cornerRadius);

        // Colored underline/accent
        g.setColour(bandCol);
        g.fillRoundedRectangle(static_cast<float>(labelX), static_cast<float>(labelY + labelHeight - 5 * scaleFactor),
                               static_cast<float>(labelWidth), 5.0f * scaleFactor, 2.0f);

        // Border
        g.setColour(bandCol.withAlpha(0.6f));
        g.drawRoundedRectangle(static_cast<float>(labelX), static_cast<float>(labelY),
                               static_cast<float>(labelWidth), static_cast<float>(labelHeight), cornerRadius, 1.5f);

        // Text
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f * scaleFactor).withStyle("Bold"));
        g.drawText(getBandName(currentBand) + " Band",
                   labelX, labelY, labelWidth, labelHeight - static_cast<int>(5 * scaleFactor),
                   juce::Justification::centred);
    }

    static juce::String getBandName(int band)
    {
        const char* names[] = {"Low", "Lo-Mid", "Hi-Mid", "High"};
        return names[juce::jlimit(0, 3, band)];
    }

    static juce::String formatFrequency(float freq)
    {
        if (freq >= 1000.0f)
            return juce::String(freq / 1000.0f, 1) + " kHz";
        else
            return juce::String(static_cast<int>(freq)) + " Hz";
    }

    void selectBand(int band)
    {
        currentBand = juce::jlimit(0, 3, band);

        const juce::String bandNames[] = {"low", "lowmid", "highmid", "high"};
        const juce::String& bandName = bandNames[currentBand];

        // Destroy old attachments first
        thresholdAttachment.reset();
        ratioAttachment.reset();
        attackAttachment.reset();
        releaseAttachment.reset();
        makeupAttachment.reset();

        // Create new attachments for selected band
        thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mb_" + bandName + "_threshold", bandThreshold);
        ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mb_" + bandName + "_ratio", bandRatio);
        attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mb_" + bandName + "_attack", bandAttack);
        releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mb_" + bandName + "_release", bandRelease);
        makeupAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mb_" + bandName + "_makeup", bandMakeup);

        repaint();
    }
};

//==============================================================================
// Studio VCA Panel (precision red style)
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
        LunaSliderStyle::configureKnob(thresholdSlider);
        thresholdSlider.setRange(-40.0, 20.0, 0.1);
        thresholdSlider.setTextValueSuffix(" dB");
        thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Ratio control (1:1 to 10:1)
        addAndMakeVisible(ratioSlider);
        ratioSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(ratioSlider);
        ratioSlider.setRange(1.0, 10.0, 0.1);
        ratioSlider.setTextValueSuffix(":1");
        ratioSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Attack control (0.3 to 75 ms)
        addAndMakeVisible(attackSlider);
        attackSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(attackSlider);
        attackSlider.setRange(0.3, 75.0, 0.1);
        attackSlider.setSkewFactorFromMidPoint(10.0);
        attackSlider.setTextValueSuffix(" ms");
        attackSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Release control (50 to 3000 ms)
        addAndMakeVisible(releaseSlider);
        releaseSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(releaseSlider);
        releaseSlider.setRange(50.0, 3000.0, 1.0);
        releaseSlider.setSkewFactorFromMidPoint(300.0);
        releaseSlider.setTextValueSuffix(" ms");
        releaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Output/Makeup gain (-20 to +20 dB)
        addAndMakeVisible(outputSlider);
        outputSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(outputSlider);
        outputSlider.setRange(-20.0, 20.0, 0.1);
        outputSlider.setTextValueSuffix(" dB");
        outputSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

        // Mix control (0 to 100%)
        addAndMakeVisible(mixSlider);
        mixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        LunaSliderStyle::configureKnob(mixSlider);
        mixSlider.setRange(0.0, 100.0, 1.0);
        mixSlider.setTextValueSuffix(" %");
        mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
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
        // Use global mix parameter for consistency across all modes
        mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters, "mix", mixSlider);
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

        auto knobWidth = controlRow.getWidth() / 6;

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
        layoutKnob(outputSlider, controlRow.removeFromLeft(knobWidth));
        layoutKnob(mixSlider, controlRow);
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

        // Title and description are drawn by main editor for consistency with other modes
    }

private:
    juce::AudioProcessorValueTreeState& parameters;
    float currentScaleFactor = 1.0f;

    juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider, outputSlider, mixSlider;

    std::vector<std::unique_ptr<juce::Label>> labels;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    void createLabels()
    {
        addLabel("THRESHOLD", thresholdSlider);
        addLabel("RATIO", ratioSlider);
        addLabel("ATTACK", attackSlider);
        addLabel("RELEASE", releaseSlider);
        addLabel("OUTPUT", outputSlider);
        addLabel("MIX", mixSlider);
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
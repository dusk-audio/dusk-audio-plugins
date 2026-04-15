#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Modal settings overlay for rarely-changed spectrum analyzer parameters.
    Click outside the panel to dismiss.
*/
class SettingsOverlay : public juce::Component
{
public:
    SettingsOverlay()
    {
        setInterceptsMouseClicks(true, true);

        // Smoothing
        smoothingLabel.setText("Smoothing", juce::dontSendNotification);
        smoothingLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        smoothingLabel.setJustificationType(juce::Justification::centredRight);
        panel.addAndMakeVisible(smoothingLabel);

        smoothingSlider.setRange(0.0, 1.0, 0.01);
        smoothingSlider.setValue(0.5);
        smoothingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        smoothingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        smoothingSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffaaaaaa));
        smoothingSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        panel.addAndMakeVisible(smoothingSlider);

        // Slope
        slopeLabel.setText("Slope", juce::dontSendNotification);
        slopeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        slopeLabel.setJustificationType(juce::Justification::centredRight);
        panel.addAndMakeVisible(slopeLabel);

        slopeSlider.setRange(-4.5, 4.5, 0.5);
        slopeSlider.setValue(0.0);
        slopeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        slopeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        slopeSlider.setTextValueSuffix(" dB");
        slopeSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffaaaaaa));
        slopeSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        panel.addAndMakeVisible(slopeSlider);

        // Decay
        decayLabel.setText("Decay", juce::dontSendNotification);
        decayLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        decayLabel.setJustificationType(juce::Justification::centredRight);
        panel.addAndMakeVisible(decayLabel);

        decaySlider.setRange(3.0, 60.0, 1.0);
        decaySlider.setValue(20.0);
        decaySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        decaySlider.setSliderStyle(juce::Slider::LinearHorizontal);
        decaySlider.setTextValueSuffix(" dB/s");
        decaySlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffaaaaaa));
        decaySlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        panel.addAndMakeVisible(decaySlider);

        // Range
        rangeLabel.setText("Range", juce::dontSendNotification);
        rangeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        rangeLabel.setJustificationType(juce::Justification::centredRight);
        panel.addAndMakeVisible(rangeLabel);

        rangeSlider.setRange(-100.0, -30.0, 1.0);
        rangeSlider.setValue(-60.0);
        rangeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        rangeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        rangeSlider.setTextValueSuffix(" dB");
        rangeSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffaaaaaa));
        rangeSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        panel.addAndMakeVisible(rangeSlider);

        // Peak Hold
        peakHoldLabel.setText("Peak Hold", juce::dontSendNotification);
        peakHoldLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        peakHoldLabel.setJustificationType(juce::Justification::centredRight);
        panel.addAndMakeVisible(peakHoldLabel);

        peakHoldButton.setButtonText("Enabled");
        peakHoldButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff888888));
        peakHoldButton.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff00aaff));
        panel.addAndMakeVisible(peakHoldButton);

        addAndMakeVisible(panel);
    }

    void paint(juce::Graphics& g) override
    {
        // Dark overlay background
        g.fillAll(juce::Colour(0xd0101010));

        // Panel background
        auto panelBounds = getPanelBounds();
        g.setColour(juce::Colour(0xff252525));
        g.fillRoundedRectangle(panelBounds.toFloat(), 8.0f);

        // Panel border
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawRoundedRectangle(panelBounds.toFloat().reduced(0.5f), 8.0f, 1.5f);

        // Title
        auto titleArea = panelBounds.removeFromTop(36);
        g.setColour(juce::Colour(0xff00aaff));
        g.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
        g.drawText("Settings", titleArea.reduced(16, 0), juce::Justification::centredLeft);

        // Divider
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawHorizontalLine(titleArea.getBottom(), static_cast<float>(panelBounds.getX() + 12),
            static_cast<float>(panelBounds.getRight() - 12));
    }

    void resized() override
    {
        auto panelBounds = getPanelBounds();
        panel.setBounds(panelBounds);

        auto area = panelBounds.withZeroOrigin(); // local coords for panel
        area.removeFromTop(40); // title + divider
        area.reduce(16, 8);

        constexpr int rowH = 32;
        constexpr int labelW = 80;
        constexpr int gap = 6;

        auto layoutRow = [&](juce::Label& label, juce::Component& control) {
            auto row = area.removeFromTop(rowH);
            label.setBounds(row.removeFromLeft(labelW));
            row.removeFromLeft(gap);
            control.setBounds(row);
            area.removeFromTop(4); // row spacing
        };

        layoutRow(smoothingLabel, smoothingSlider);
        layoutRow(slopeLabel, slopeSlider);
        layoutRow(decayLabel, decaySlider);
        layoutRow(rangeLabel, rangeSlider);
        layoutRow(peakHoldLabel, peakHoldButton);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Click outside panel to dismiss
        if (!getPanelBounds().contains(e.getPosition()))
        {
            if (onDismiss)
                onDismiss();
        }
    }

    //==========================================================================
    juce::Slider& getSmoothingSlider() { return smoothingSlider; }
    juce::Slider& getSlopeSlider() { return slopeSlider; }
    juce::Slider& getDecaySlider() { return decaySlider; }
    juce::Slider& getRangeSlider() { return rangeSlider; }
    juce::ToggleButton& getPeakHoldButton() { return peakHoldButton; }

    std::function<void()> onDismiss;

private:
    juce::Rectangle<int> getPanelBounds() const
    {
        constexpr int panelW = 360;
        constexpr int panelH = 240;
        return getLocalBounds().withSizeKeepingCentre(panelW, panelH);
    }

    juce::Component panel; // container for controls (positioned within panel bounds)

    juce::Label smoothingLabel;
    juce::Slider smoothingSlider;

    juce::Label slopeLabel;
    juce::Slider slopeSlider;

    juce::Label decayLabel;
    juce::Slider decaySlider;

    juce::Label rangeLabel;
    juce::Slider rangeSlider;

    juce::Label peakHoldLabel;
    juce::ToggleButton peakHoldButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

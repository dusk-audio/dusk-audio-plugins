#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Custom LookAndFeel for Spectrum Analyzer
*/
class SpectrumAnalyzerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    //==========================================================================
    // Color scheme
    struct Colors
    {
        static constexpr juce::uint32 background = 0xff1a1a1a;
        static constexpr juce::uint32 panelBg = 0xff252525;
        static constexpr juce::uint32 border = 0xff3a3a3a;
        static constexpr juce::uint32 textPrimary = 0xffffffff;
        static constexpr juce::uint32 textSecondary = 0xff888888;
        static constexpr juce::uint32 accent = 0xff00aaff;
        static constexpr juce::uint32 accentDim = 0xff006699;

        // Spectrum colors
        static constexpr juce::uint32 spectrumFill = 0xff00aaff;
        static constexpr juce::uint32 peakHold = 0xffffaa00;

        // Meter colors
        static constexpr juce::uint32 meterGreen = 0xff00cc00;
        static constexpr juce::uint32 meterYellow = 0xffcccc00;
        static constexpr juce::uint32 meterRed = 0xffcc0000;
    };

    //==========================================================================
    SpectrumAnalyzerLookAndFeel()
    {
        // Set default colors
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(Colors::background));
        setColour(juce::Label::textColourId, juce::Colour(Colors::textPrimary));

        setColour(juce::ComboBox::backgroundColourId, juce::Colour(Colors::panelBg));
        setColour(juce::ComboBox::textColourId, juce::Colour(Colors::textPrimary));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(Colors::border));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(Colors::textSecondary));

        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(Colors::panelBg));
        setColour(juce::PopupMenu::textColourId, juce::Colour(Colors::textPrimary));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(Colors::accent));

        setColour(juce::Slider::thumbColourId, juce::Colour(Colors::accent));
        setColour(juce::Slider::trackColourId, juce::Colour(Colors::border));
        setColour(juce::Slider::backgroundColourId, juce::Colour(Colors::panelBg));

        setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::textSecondary));
        setColour(juce::ToggleButton::tickColourId, juce::Colour(Colors::accent));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(Colors::border));
    }

    //==========================================================================
    void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 3.0f);

        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

        // Arrow
        auto arrowZone = bounds.removeFromRight(20.0f).reduced(6.0f);
        juce::Path arrow;
        arrow.addTriangle(arrowZone.getX(), arrowZone.getCentreY() - 3.0f,
                          arrowZone.getRight(), arrowZone.getCentreY() - 3.0f,
                          arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);

        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.fillPath(arrow);
    }

    //==========================================================================
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                sliderPos, 0, 0, style, slider);
            return;
        }

        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
        auto trackBounds = bounds.reduced(2.0f, bounds.getHeight() * 0.35f);

        // Track background
        g.setColour(slider.findColour(juce::Slider::backgroundColourId));
        g.fillRoundedRectangle(trackBounds, 2.0f);

        // Filled portion
        auto filledBounds = trackBounds.withWidth(sliderPos - static_cast<float>(x));
        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        g.fillRoundedRectangle(filledBounds, 2.0f);

        // Thumb
        float thumbWidth = 8.0f;
        float thumbX = sliderPos - thumbWidth * 0.5f;
        auto thumbBounds = juce::Rectangle<float>(thumbX, bounds.getY() + 2.0f,
            thumbWidth, bounds.getHeight() - 4.0f);

        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        g.fillRoundedRectangle(thumbBounds, 2.0f);
    }

    //==========================================================================
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        float tickSize = 14.0f;

        auto tickBounds = bounds.removeFromLeft(tickSize + 4.0f)
            .withSizeKeepingCentre(tickSize, tickSize);

        // Checkbox background
        g.setColour(juce::Colour(Colors::panelBg));
        g.fillRoundedRectangle(tickBounds, 2.0f);

        g.setColour(juce::Colour(Colors::border));
        g.drawRoundedRectangle(tickBounds, 2.0f, 1.0f);

        // Checkmark
        if (button.getToggleState())
        {
            g.setColour(button.findColour(juce::ToggleButton::tickColourId));
            auto checkBounds = tickBounds.reduced(3.0f);

            juce::Path checkPath;
            checkPath.startNewSubPath(checkBounds.getX(), checkBounds.getCentreY());
            checkPath.lineTo(checkBounds.getCentreX() - 1.0f, checkBounds.getBottom() - 2.0f);
            checkPath.lineTo(checkBounds.getRight(), checkBounds.getY() + 2.0f);

            g.strokePath(checkPath, juce::PathStrokeType(2.0f));
        }

        // Text
        g.setColour(shouldDrawButtonAsHighlighted ?
            juce::Colour(Colors::textPrimary) :
            button.findColour(juce::ToggleButton::textColourId));
        g.setFont(12.0f);
        g.drawText(button.getButtonText(), bounds.reduced(4.0f, 0.0f),
            juce::Justification::centredLeft);
    }

    //==========================================================================
    // Static helper for drawing section panels
    static void drawSectionPanel(juce::Graphics& g, juce::Rectangle<int> bounds,
                                  const juce::String& title = "")
    {
        g.setColour(juce::Colour(Colors::panelBg));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

        if (title.isNotEmpty())
        {
            g.setColour(juce::Colour(Colors::textSecondary));
            g.setFont(10.0f);
            g.drawText(title, bounds.removeFromTop(16).reduced(8, 0),
                juce::Justification::centredLeft);
        }
    }
};

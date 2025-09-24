#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class LunaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Unified color scheme for Luna Co. Audio plugins
    static constexpr auto BACKGROUND_COLOR = 0xff1e1e1e;      // Dark charcoal
    static constexpr auto PANEL_COLOR = 0xff2a2a2a;           // Panel sections
    static constexpr auto ACCENT_COLOR = 0xffff6b35;          // Warm orange (brand color)
    static constexpr auto TEXT_COLOR = 0xffd4d4d4;            // Light grey text
    static constexpr auto BORDER_COLOR = 0xff3a3a3a;          // Dividers/borders
    static constexpr auto KNOB_BODY_COLOR = 0xff4a4a4a;       // Knob body
    static constexpr auto SHADOW_COLOR = 0x60000000;          // Shadows

    LunaLookAndFeel()
    {
        // Set default colors for all components
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(BACKGROUND_COLOR));
        setColour(juce::Slider::thumbColourId, juce::Colour(ACCENT_COLOR));
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(ACCENT_COLOR));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(BORDER_COLOR));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(TEXT_COLOR));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(PANEL_COLOR));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(BORDER_COLOR));

        setColour(juce::Label::textColourId, juce::Colour(TEXT_COLOR));
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        setColour(juce::TextButton::buttonColourId, juce::Colour(PANEL_COLOR));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(ACCENT_COLOR));
        setColour(juce::TextButton::textColourOffId, juce::Colour(TEXT_COLOR));
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);

        setColour(juce::ComboBox::backgroundColourId, juce::Colour(PANEL_COLOR));
        setColour(juce::ComboBox::textColourId, juce::Colour(TEXT_COLOR));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff808080));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(BORDER_COLOR));

        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(PANEL_COLOR));
        setColour(juce::PopupMenu::textColourId, juce::Colour(TEXT_COLOR));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(ACCENT_COLOR));

        setColour(juce::ToggleButton::textColourId, juce::Colour(TEXT_COLOR));
        setColour(juce::ToggleButton::tickColourId, juce::Colour(ACCENT_COLOR));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff808080));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
        auto centreX = x + width * 0.5f;
        auto centreY = y + height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Shadow
        g.setColour(juce::Colour(SHADOW_COLOR));
        g.fillEllipse(rx + 2, ry + 2, rw, rw);

        // Outer metallic ring
        juce::ColourGradient outerGradient(
            juce::Colour(0xff5a5a5a), centreX - radius, centreY,
            juce::Colour(0xff2a2a2a), centreX + radius, centreY, false);
        g.setGradientFill(outerGradient);
        g.fillEllipse(rx - 3, ry - 3, rw + 6, rw + 6);

        // Inner knob body
        juce::ColourGradient bodyGradient(
            juce::Colour(KNOB_BODY_COLOR), centreX - radius * 0.7f, centreY - radius * 0.7f,
            juce::Colour(0xff1a1a1a), centreX + radius * 0.7f, centreY + radius * 0.7f, true);
        g.setGradientFill(bodyGradient);
        g.fillEllipse(rx, ry, rw, rw);

        // Inner ring detail
        g.setColour(juce::Colour(PANEL_COLOR));
        g.drawEllipse(rx + 4, ry + 4, rw - 8, rw - 8, 2.0f);

        // Center cap
        auto capRadius = radius * 0.3f;
        juce::ColourGradient capGradient(
            juce::Colour(0xff6a6a6a), centreX - capRadius, centreY - capRadius,
            juce::Colour(BORDER_COLOR), centreX + capRadius, centreY + capRadius, false);
        g.setGradientFill(capGradient);
        g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

        // Position indicator with glow
        juce::Path pointer;
        pointer.addRectangle(-2.0f, -radius + 6, 4.0f, radius * 0.4f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        // Orange glow effect
        g.setColour(juce::Colour(ACCENT_COLOR).withAlpha(0.3f));
        g.strokePath(pointer, juce::PathStrokeType(6.0f));
        g.setColour(juce::Colour(ACCENT_COLOR));
        g.fillPath(pointer);

        // Tick marks
        g.setColour(juce::Colour(0xffaaaaaa).withAlpha(0.7f));
        for (int i = 0; i <= 10; ++i)
        {
            auto tickAngle = rotaryStartAngle + (i / 10.0f) * (rotaryEndAngle - rotaryStartAngle);
            auto tickLength = (i == 0 || i == 5 || i == 10) ? radius * 0.15f : radius * 0.1f;

            juce::Path tick;
            tick.addRectangle(-1.0f, -radius - 8, 2.0f, tickLength);
            tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(centreX, centreY));
            g.fillPath(tick);
        }

        // Center screw detail
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillEllipse(centreX - 3, centreY - 3, 6, 6);
        g.setColour(juce::Colour(KNOB_BODY_COLOR));
        g.drawEllipse(centreX - 3, centreY - 3, 6, 6, 0.5f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);

        // LED-style indicator
        auto ledBounds = bounds.removeFromLeft(20);
        g.setColour(button.getToggleState() ? juce::Colour(ACCENT_COLOR) : juce::Colour(PANEL_COLOR));
        g.fillEllipse(ledBounds.reduced(2));

        // LED glow when on
        if (button.getToggleState())
        {
            g.setColour(juce::Colour(ACCENT_COLOR).withAlpha(0.3f));
            g.fillEllipse(ledBounds);
        }

        g.setColour(juce::Colour(BORDER_COLOR));
        g.drawEllipse(ledBounds.reduced(2), 1.0f);

        // Text
        g.setColour(button.getToggleState() ? juce::Colours::white : juce::Colour(TEXT_COLOR));
        g.setFont(12.0f);
        g.drawText(button.getButtonText(), bounds, juce::Justification::centredLeft);

        if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colour(ACCENT_COLOR).withAlpha(0.2f));
            g.drawRoundedRectangle(button.getLocalBounds().toFloat(), 3.0f, 1.0f);
        }
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                             const juce::Colour& backgroundColour,
                             bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto baseColour = backgroundColour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);

        if (shouldDrawButtonAsDown || button.getToggleState())
            baseColour = juce::Colour(ACCENT_COLOR);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.2f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(juce::Colour(BORDER_COLOR));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                     int, int, int, int, juce::ComboBox& box) override
    {
        auto cornerSize = 4.0f;
        juce::Rectangle<int> boxBounds(0, 0, width, height);

        g.setColour(juce::Colour(PANEL_COLOR));
        g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);

        g.setColour(juce::Colour(BORDER_COLOR));
        g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f), cornerSize, 1.0f);

        // Arrow
        juce::Rectangle<int> arrowZone(width - 30, 0, 20, height);
        juce::Path path;
        path.startNewSubPath(arrowZone.getX() + 3.0f, arrowZone.getCentreY() - 2.0f);
        path.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
        path.lineTo(arrowZone.getRight() - 3.0f, arrowZone.getCentreY() - 2.0f);

        g.setColour(juce::Colour(TEXT_COLOR).withAlpha(box.isEnabled() ? 0.9f : 0.2f));
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

    // Helper function to draw the standard plugin header
    static void drawPluginHeader(juce::Graphics& g, juce::Rectangle<int> bounds,
                                 const juce::String& pluginName, const juce::String& subtitle = "")
    {
        // Header background
        auto headerBounds = bounds.removeFromTop(50);
        g.setColour(juce::Colour(0xff161616));
        g.fillRect(headerBounds);

        // Bottom border
        g.setColour(juce::Colour(BORDER_COLOR));
        g.drawHorizontalLine(headerBounds.getBottom(), 0, headerBounds.getWidth());

        // Plugin name
        g.setColour(juce::Colour(TEXT_COLOR));
        g.setFont(juce::Font(24.0f, juce::Font::bold));
        g.drawText(pluginName, headerBounds.removeFromLeft(300), juce::Justification::centred);

        // Subtitle if provided
        if (subtitle.isNotEmpty())
        {
            g.setFont(juce::Font(14.0f));
            g.setColour(juce::Colour(0xff909090));
            g.drawText(subtitle, headerBounds.removeFromLeft(200), juce::Justification::centredLeft);
        }

        // Luna Co. Audio branding
        g.setFont(juce::Font(12.0f));
        g.setColour(juce::Colour(0xff808080));
        g.drawText("Luna Co. Audio", headerBounds.removeFromRight(150), juce::Justification::centred);
    }

    // For knobs that need special colors (like EQ bands)
    void drawColoredRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                 float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                 juce::Colour knobColor)
    {
        auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
        auto centreX = x + width * 0.5f;
        auto centreY = y + height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Shadow
        g.setColour(juce::Colour(SHADOW_COLOR));
        g.fillEllipse(rx + 2, ry + 2, rw, rw);

        // Outer colored ring
        juce::ColourGradient outerGradient(
            knobColor.brighter(0.3f), centreX - radius, centreY,
            knobColor.darker(0.3f), centreX + radius, centreY, false);
        g.setGradientFill(outerGradient);
        g.fillEllipse(rx - 3, ry - 3, rw + 6, rw + 6);

        // Inner knob body
        juce::ColourGradient bodyGradient(
            juce::Colour(KNOB_BODY_COLOR), centreX - radius * 0.7f, centreY - radius * 0.7f,
            juce::Colour(0xff1a1a1a), centreX + radius * 0.7f, centreY + radius * 0.7f, true);
        g.setGradientFill(bodyGradient);
        g.fillEllipse(rx, ry, rw, rw);

        // Position indicator with colored glow
        juce::Path pointer;
        pointer.addRectangle(-2.0f, -radius + 6, 4.0f, radius * 0.4f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        g.setColour(knobColor.withAlpha(0.3f));
        g.strokePath(pointer, juce::PathStrokeType(6.0f));
        g.setColour(knobColor);
        g.fillPath(pointer);

        // Tick marks
        g.setColour(juce::Colour(0xffaaaaaa).withAlpha(0.7f));
        for (int i = 0; i <= 10; ++i)
        {
            auto tickAngle = rotaryStartAngle + (i / 10.0f) * (rotaryEndAngle - rotaryStartAngle);
            auto tickLength = (i == 0 || i == 5 || i == 10) ? radius * 0.15f : radius * 0.1f;

            juce::Path tick;
            tick.addRectangle(-1.0f, -radius - 8, 2.0f, tickLength);
            tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(centreX, centreY));
            g.fillPath(tick);
        }

        // Center screw detail
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillEllipse(centreX - 3, centreY - 3, 6, 6);
        g.setColour(knobColor.darker(0.5f));
        g.drawEllipse(centreX - 3, centreY - 3, 6, 6, 0.5f);
    }
};

// Extended version for 4K EQ with colored knobs
class LunaEQLookAndFeel : public LunaLookAndFeel
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        // Determine knob color based on parameter name
        auto name = slider.getName();
        juce::Colour knobColor(ACCENT_COLOR);

        if (name.contains("lf"))
            knobColor = juce::Colour(0xffdc3545);  // Red for LF
        else if (name.contains("lmf"))
            knobColor = juce::Colour(0xffffc107);  // Yellow for LMF
        else if (name.contains("hmf"))
            knobColor = juce::Colour(0xff007bff);  // Blue for HMF
        else if (name.contains("hf"))
            knobColor = juce::Colour(0xff28a745);  // Green for HF
        else if (name.contains("hpf") || name.contains("lpf"))
            knobColor = juce::Colour(0xffb8860b);  // Brown/orange for filters
        else
        {
            // Use default for other controls
            LunaLookAndFeel::drawRotarySlider(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
            return;
        }

        drawColoredRotarySlider(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, knobColor);
    }
};
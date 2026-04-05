#include "DuskAmpLookAndFeel.h"

DuskAmpLookAndFeel::DuskAmpLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (kBackground));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0141414));
    setColour (juce::TooltipWindow::textColourId, juce::Colour (kText));
    setColour (juce::TooltipWindow::outlineColourId, juce::Colour (kBorder));

    setColour (juce::ComboBox::backgroundColourId, juce::Colour (kPanel));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (kBorder));
    setColour (juce::ComboBox::textColourId, juce::Colour (kText));
    setColour (juce::ComboBox::arrowColourId, juce::Colour (kGroupText));

    setColour (juce::TextButton::buttonColourId, juce::Colour (kPanel));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (kPanelHi));
    setColour (juce::TextButton::textColourOffId, juce::Colour (kLabelText));
    setColour (juce::TextButton::textColourOnId, juce::Colour (kValueText));

    // PopupMenu (for ComboBox dropdowns)
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (kPanel));
    setColour (juce::PopupMenu::textColourId, juce::Colour (kText));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (kAccent).withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    // ScrollBar colours (fallback, custom drawScrollbar below handles the rest)
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff333333));
    setColour (juce::ScrollBar::trackColourId, juce::Colour (kBackground));
}

// =============================================================================
// Rotary Slider — gradient knob face, drop shadow, larger dot
// =============================================================================

void DuskAmpLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                            int width, int height,
                                            float sliderPos,
                                            float rotaryStartAngle,
                                            float rotaryEndAngle,
                                            juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    float diameter = std::min (bounds.getWidth(), bounds.getHeight());
    auto centre = bounds.getCentre();
    float radius = diameter * 0.5f;

    bool isHovered  = slider.isMouseOverOrDragging();
    bool isDragging = slider.isMouseButtonDown();

    // Glow ring when dragging
    if (isDragging)
    {
        g.setColour (juce::Colour (kAccent).withAlpha (0.10f));
        g.fillEllipse (centre.x - radius, centre.y - radius,
                       radius * 2.0f, radius * 2.0f);
    }

    // Drop shadow beneath knob
    {
        float shadowR = radius - 1.0f;
        float shadowOff = 2.0f;
        g.setColour (juce::Colour (0x30000000));
        g.fillEllipse (centre.x - shadowR, centre.y - shadowR + shadowOff,
                       shadowR * 2.0f, shadowR * 2.0f);
    }

    // Outer dark ring
    float outerRadius = radius - 2.0f;
    g.setColour (juce::Colour (kKnobEdge));
    g.fillEllipse (centre.x - outerRadius, centre.y - outerRadius,
                   outerRadius * 2.0f, outerRadius * 2.0f);

    // Knob body with top-left lighting gradient + convex highlight
    float knobRadius = outerRadius - 3.0f;
    {
        auto knobCol = isHovered ? juce::Colour (kKnobFill).brighter (0.12f)
                                 : juce::Colour (kKnobFill);

        // Base: linear gradient from top-left (lighter) to bottom-right (darker)
        juce::ColourGradient knobGrad (
            knobCol.brighter (0.18f),
            centre.x - knobRadius * 0.5f, centre.y - knobRadius * 0.5f,
            knobCol.darker (0.10f),
            centre.x + knobRadius * 0.5f, centre.y + knobRadius * 0.5f,
            false);
        g.setGradientFill (knobGrad);
        g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius,
                       knobRadius * 2.0f, knobRadius * 2.0f);

        // Convex highlight: radial gradient on upper-left quadrant (simulates dome)
        float hlRadius = knobRadius * 0.75f;
        float hlOffX = -knobRadius * 0.25f;
        float hlOffY = -knobRadius * 0.25f;
        juce::ColourGradient highlight (
            juce::Colour (0x14ffffff),  // bright center
            centre.x + hlOffX, centre.y + hlOffY,
            juce::Colour (0x00ffffff),  // transparent edge
            centre.x + hlOffX + hlRadius, centre.y + hlOffY + hlRadius,
            true);  // radial
        g.setGradientFill (highlight);
        g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius,
                       knobRadius * 2.0f, knobRadius * 2.0f);

        // Bottom-edge shadow (concave undercut)
        float shadowArc = knobRadius * 0.92f;
        g.setColour (juce::Colour (0x0c000000));
        g.fillEllipse (centre.x - shadowArc, centre.y - shadowArc + 1.5f,
                       shadowArc * 2.0f, shadowArc * 2.0f);
    }

    // Arc track (background)
    float arcRadius = outerRadius - 1.5f;
    float lineW = 3.0f;
    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (kBorder));
    g.strokePath (trackArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    // Filled arc with gradient
    float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    if (angle > rotaryStartAngle + 0.01f)
    {
        juce::Path filledArc;
        filledArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                                 0.0f, rotaryStartAngle, angle, true);

        auto accentCol = juce::Colour (kAccent);
        juce::ColourGradient arcGradient (
            accentCol.darker (0.3f),
            centre.x + arcRadius * std::sin (rotaryStartAngle),
            centre.y - arcRadius * std::cos (rotaryStartAngle),
            isDragging ? accentCol.brighter (0.2f) : accentCol,
            centre.x + arcRadius * std::sin (angle),
            centre.y - arcRadius * std::cos (angle),
            false);
        g.setGradientFill (arcGradient);
        g.strokePath (filledArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
    }

    // Dot indicator
    float dotRadius = 3.5f;
    float dotDist = knobRadius - 6.0f;
    float dotX = centre.x + dotDist * std::sin (angle);
    float dotY = centre.y - dotDist * std::cos (angle);
    g.setColour (isDragging ? juce::Colours::white : juce::Colour (kValueText));
    g.fillEllipse (dotX - dotRadius, dotY - dotRadius,
                   dotRadius * 2.0f, dotRadius * 2.0f);

    // Value text inside large knobs
    if (diameter >= 70.0f)
    {
        auto text = slider.getTextFromValue (slider.getValue());
        g.setColour (juce::Colour (kValueText));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text, bounds.toNearestInt(), juce::Justification::centred);
    }
}

// =============================================================================
// Label
// =============================================================================

void DuskAmpLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (label.getFont());
    g.drawFittedText (label.getText(), label.getLocalBounds(),
                      label.getJustificationType(), 1);
}

// =============================================================================
// Toggle Button — LED indicator + pill shape
// =============================================================================

void DuskAmpLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (4.0f);
    bool on = button.getToggleState();
    auto accent = juce::Colour (kAccent);
    float cornerSize = bounds.getHeight() * 0.5f;

    // LED geometry
    float ledR = 3.0f;
    float ledX = bounds.getX() + 10.0f;
    float ledY = bounds.getCentreY();

    if (on)
    {
        // Subtle outer glow
        g.setColour (accent.withAlpha (0.25f));
        g.fillRoundedRectangle (bounds.expanded (1.5f), cornerSize + 1.5f);

        // Filled pill
        g.setColour (accent);
        g.fillRoundedRectangle (bounds, cornerSize);

        // Inner shadow (pressed look)
        g.setColour (juce::Colour (0x18000000));
        g.fillRoundedRectangle (bounds.reduced (1.0f).translated (0.0f, 1.0f), cornerSize - 1.0f);

        // LED dot: bright white with amber glow
        g.setColour (accent.withAlpha (0.4f));
        g.fillEllipse (ledX - ledR * 2.0f, ledY - ledR * 2.0f, ledR * 4.0f, ledR * 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.fillEllipse (ledX - ledR, ledY - ledR, ledR * 2.0f, ledR * 2.0f);

        g.setColour (juce::Colours::white);
    }
    else
    {
        // Inactive background — slightly lighter on hover
        g.setColour (shouldDrawButtonAsHighlighted ? juce::Colour (kPanelHi) : juce::Colour (kPanel));
        g.fillRoundedRectangle (bounds, cornerSize);

        // Crisp border — brighter on hover
        g.setColour (shouldDrawButtonAsHighlighted ? juce::Colour (kBorderHi) : juce::Colour (kBorder));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerSize, 1.0f);

        // LED dot: empty ring
        g.setColour (juce::Colour (kBorder));
        g.drawEllipse (ledX - ledR, ledY - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);

        // Text colour: lighter than before so it doesn't look disabled
        g.setColour (shouldDrawButtonAsHighlighted ? juce::Colour (kLabelText) : juce::Colour (kGroupText));
    }

    // Text (offset right to make room for LED)
    auto textBounds = bounds.withTrimmedLeft (10.0f);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (button.getButtonText(), textBounds, juce::Justification::centred);
}

// =============================================================================
// Scrollbar — dark theme (no bright OS-blue)
// =============================================================================

void DuskAmpLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& /*scrollbar*/,
                                         int x, int y, int width, int height,
                                         bool isScrollbarVertical,
                                         int thumbStartPosition, int thumbSize,
                                         bool isMouseOver, bool isMouseDown)
{
    // Track background
    g.setColour (juce::Colour (kBackground));
    g.fillRect (x, y, width, height);

    // Thumb
    auto thumbColour = isMouseDown  ? juce::Colour (kBorderHi)
                     : isMouseOver  ? juce::Colour (kBorder)
                                    : juce::Colour (0xff2a2a2a);

    juce::Rectangle<int> thumbBounds;
    if (isScrollbarVertical)
        thumbBounds = { x + 1, thumbStartPosition, width - 2, thumbSize };
    else
        thumbBounds = { thumbStartPosition, y + 1, thumbSize, height - 2 };

    float cr = isScrollbarVertical ? static_cast<float> (width) * 0.4f
                                   : static_cast<float> (height) * 0.4f;
    g.setColour (thumbColour);
    g.fillRoundedRectangle (thumbBounds.toFloat(), cr);
}

// =============================================================================
// ComboBox — left-aligned text with padding
// =============================================================================

void DuskAmpLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                        bool /*isButtonDown*/,
                                        int /*buttonX*/, int /*buttonY*/,
                                        int /*buttonW*/, int /*buttonH*/,
                                        juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();
    float cr = 4.0f;

    // Background
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, cr);

    // Border
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cr, 1.0f);

    // Arrow
    float arrowSize = static_cast<float> (height) * 0.3f;
    float arrowX = static_cast<float> (width) - arrowSize * 2.0f;
    float arrowY = (static_cast<float> (height) - arrowSize) * 0.5f;

    juce::Path arrow;
    arrow.addTriangle (arrowX, arrowY,
                       arrowX + arrowSize * 1.2f, arrowY,
                       arrowX + arrowSize * 0.6f, arrowY + arrowSize * 0.7f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

// =============================================================================
// TextButton — subtle dark button matching Browse style
// =============================================================================

void DuskAmpLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& backgroundColour,
                                                bool shouldDrawButtonAsHighlighted,
                                                bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    float cr = 4.0f;

    auto bgColour = shouldDrawButtonAsDown   ? backgroundColour.brighter (0.15f)
                  : shouldDrawButtonAsHighlighted ? backgroundColour.brighter (0.08f)
                  : backgroundColour;

    g.setColour (bgColour);
    g.fillRoundedRectangle (bounds, cr);

    // Border — brighter on hover
    g.setColour (shouldDrawButtonAsHighlighted ? juce::Colour (kBorderHi) : juce::Colour (kBorder));
    g.drawRoundedRectangle (bounds, cr, 1.0f);
}

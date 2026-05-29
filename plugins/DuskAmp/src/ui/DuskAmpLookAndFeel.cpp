#include "DuskAmpLookAndFeel.h"

DuskAmpLookAndFeel::DuskAmpLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (kBackground));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    // Tooltip — opaque panel + faint border, no glow.
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf01a1a1c));
    setColour (juce::TooltipWindow::textColourId, juce::Colour (kValueText));
    setColour (juce::TooltipWindow::outlineColourId, juce::Colour (kBorder));

    // ComboBox: flat, no shadow.
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (kKnobFill));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (kBorder));
    setColour (juce::ComboBox::textColourId, juce::Colour (kValueText));
    setColour (juce::ComboBox::arrowColourId, juce::Colour (kGroupText));
    setColour (juce::ComboBox::buttonColourId, juce::Colour (kKnobFill));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (kPanel));
    setColour (juce::PopupMenu::textColourId, juce::Colour (kValueText));
    setColour (juce::PopupMenu::highlightedBackgroundColourId,
               juce::Colour (kAccent).withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (kAccent));

    // TextButton: muted styling for utility buttons (Save/Delete/Browse).
    setColour (juce::TextButton::buttonColourId, juce::Colour (kKnobFill));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (kKnobFill));
    setColour (juce::TextButton::textColourOffId, juce::Colour (kLabelText));
    setColour (juce::TextButton::textColourOnId, juce::Colour (kValueText));
}

// ---------------------------------------------------------------------------
// Rotary slider — flat circle, thin arc track, accent fill, thick line
// indicator from inner radius outward. No gradients, no glows. Value text
// inside the knob for large sizes (e.g. OUTPUT).
// ---------------------------------------------------------------------------
void DuskAmpLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                            int width, int height,
                                            float sliderPos,
                                            float rotaryStartAngle,
                                            float rotaryEndAngle,
                                            juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float diameter = std::min (bounds.getWidth(), bounds.getHeight());
    auto centre = bounds.getCentre();
    const float radius = diameter * 0.5f;
    const bool dragging = slider.isMouseButtonDown();
    const bool hovered  = slider.isMouseOverOrDragging();

    // Outer rim — vertical chrome gradient (bright top → dark bottom) so
    // the knob reads as a turned-metal bezel with a top-light catch.
    const float rimR = radius - 0.5f;
    {
        juce::ColourGradient rimGrad (
            juce::Colour (0xffb8b8bc), centre.x, centre.y - rimR,
            juce::Colour (0xff2a2a2e), centre.x, centre.y + rimR,
            false);
        rimGrad.addColour (0.45, juce::Colour (0xff6a6a6e));
        rimGrad.addColour (0.55, juce::Colour (0xff48484c));
        g.setGradientFill (rimGrad);
        g.fillEllipse (centre.x - rimR, centre.y - rimR, rimR * 2.0f, rimR * 2.0f);
    }

    // Knob body — slight radial gradient for depth (top lighter, bottom darker)
    const float bodyR = radius - 4.0f;
    juce::ColourGradient bodyGrad (
        juce::Colour (kKnobFill).brighter (hovered ? 0.10f : 0.04f),
        centre.x, centre.y - bodyR,
        juce::Colour (kKnobFill).darker (0.20f),
        centre.x, centre.y + bodyR,
        false);
    g.setGradientFill (bodyGrad);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    // Inner concentric ring — adds depth + reads as turned metal
    const float innerR = bodyR * 0.62f;
    g.setColour (juce::Colour (kBorder).withAlpha (0.5f));
    g.drawEllipse (centre.x - innerR, centre.y - innerR,
                   innerR * 2.0f, innerR * 2.0f, 1.0f);

    // Track ring — thin neutral arc, full sweep.
    const float ringR = radius - 1.5f;
    const float lineW = 2.0f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (kBorder));
    g.strokePath (track, juce::PathStrokeType (lineW));

    // Decorative tick marks at start / middle / end of arc travel — small
    // radial pips just outside the arc. Subtle scale-cue without numbers.
    {
        g.setColour (juce::Colour (kGroupText).withAlpha (0.5f));
        const float tickInner = ringR + 1.5f;
        const float tickOuter = ringR + 4.0f;
        for (float t : { 0.0f, 0.5f, 1.0f })
        {
            const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const float c = std::cos (a - juce::MathConstants<float>::halfPi);
            const float s = std::sin (a - juce::MathConstants<float>::halfPi);
            g.drawLine (centre.x + c * tickInner, centre.y + s * tickInner,
                        centre.x + c * tickOuter, centre.y + s * tickOuter, 1.0f);
        }
    }

    // Value arc — single solid colour, slightly brighter when dragging.
    const float currentAngle = rotaryStartAngle
                              + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    if (currentAngle > rotaryStartAngle + 0.001f)
    {
        juce::Path fill;
        fill.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                            rotaryStartAngle, currentAngle, true);
        g.setColour (dragging ? juce::Colour (kAccent).brighter (0.15f)
                              : juce::Colour (kAccent));
        g.strokePath (fill, juce::PathStrokeType (lineW + 0.5f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Position indicator — thick line from inner ring to outer rim.
    const float indicatorInner = innerR + 1.5f;
    const float indicatorOuter = bodyR - 3.0f;
    const float ca = std::cos (currentAngle - juce::MathConstants<float>::halfPi);
    const float sa = std::sin (currentAngle - juce::MathConstants<float>::halfPi);
    juce::Line<float> indicator (centre.x + ca * indicatorInner,
                                  centre.y + sa * indicatorInner,
                                  centre.x + ca * indicatorOuter,
                                  centre.y + sa * indicatorOuter);
    g.setColour (juce::Colour (kValueText));
    g.drawLine (indicator, 2.5f);

    // Centre dot (visual anchor)
    g.setColour (juce::Colour (kBorder));
    g.fillEllipse (centre.x - 1.5f, centre.y - 1.5f, 3.0f, 3.0f);

    // Value text inside knobs — only for truly oversized rotaries (>=120 px)
    // so the standard 84-px equipment-strip knobs don't double-show the
    // value (external valueLabel does the readout).
    if (diameter >= 120.0f)
    {
        auto text = slider.getTextFromValue (slider.getValue());
        g.setColour (juce::Colour (kValueText));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text, bounds.toNearestInt(), juce::Justification::centred);
    }
}

void DuskAmpLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (label.getFont());
    g.drawFittedText (label.getText(), label.getLocalBounds(),
                      label.getJustificationType(), 1);
}

// ---------------------------------------------------------------------------
// Toggle button — capsule. OFF: hollow with border + neutral text. ON: solid
// accent fill + white text. No drop shadow / no glow.
// ---------------------------------------------------------------------------
void DuskAmpLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                            bool /*shouldDrawButtonAsHighlighted*/,
                                            bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = button.getToggleState();
    const float corner = bounds.getHeight() * 0.5f;

    if (on)
    {
        g.setColour (juce::Colour (kAccent));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (juce::Colours::white);
    }
    else
    {
        g.setColour (juce::Colour (kKnobFill));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (juce::Colour (kBorder));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
        g.setColour (juce::Colour (kLabelText));
    }

    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred);
}

// ---------------------------------------------------------------------------
// ComboBox — flat dark capsule. Subtle border. Chevron in dim grey.
// ---------------------------------------------------------------------------
void DuskAmpLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                        bool /*isButtonDown*/,
                                        int buttonX, int buttonY, int buttonW, int buttonH,
                                        juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();
    const float corner = 4.0f;

    g.setColour (juce::Colour (kKnobFill));
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (juce::Colour (kBorder));
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

    // Chevron — minimal triangle in dim grey.
    juce::Path arrow;
    const float ax = static_cast<float> (buttonX) + buttonW * 0.5f;
    const float ay = static_cast<float> (buttonY) + buttonH * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f,
                        ax + 4.0f, ay - 2.0f,
                        ax,        ay + 3.0f);
    g.setColour (box.isEnabled() ? juce::Colour (kLabelText)
                                  : juce::Colour (kDimText));
    g.fillPath (arrow);
}

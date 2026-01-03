#include "AnalogLookAndFeel.h"
#include "UniversalCompressor.h"
#include <cmath>

//==============================================================================
// Base class implementation
void AnalogLookAndFeelBase::drawMetallicKnob(juce::Graphics& g, float x, float y, 
                                             float width, float height,
                                             float sliderPos, float rotaryStartAngle, 
                                             float rotaryEndAngle,
                                             juce::Slider& slider)
{
    auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
    auto centreX = x + width * 0.5f;
    auto centreY = y + height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    
    // Drop shadow
    g.setColour(colors.shadow.withAlpha(0.5f));
    g.fillEllipse(rx + 2, ry + 2, rw, rw);
    
    // Outer bezel (metallic ring)
    juce::ColourGradient bezel(juce::Colour(0xFF8A8A8A), centreX - radius, centreY,
                               juce::Colour(0xFF3A3A3A), centreX + radius, centreY, false);
    g.setGradientFill(bezel);
    g.fillEllipse(rx - 3, ry - 3, rw + 6, rw + 6);
    
    // Inner bezel highlight
    g.setColour(juce::Colour(0xFFBABABA));
    g.drawEllipse(rx - 2, ry - 2, rw + 4, rw + 4, 1.0f);
    
    // Main knob body with brushed metal texture
    juce::ColourGradient knobGradient(colors.knobBody.brighter(0.3f), centreX, ry,
                                      colors.knobBody.darker(0.3f), centreX, ry + rw, false);
    g.setGradientFill(knobGradient);
    g.fillEllipse(rx, ry, rw, rw);
    
    // Center cap with subtle gradient
    auto capRadius = radius * 0.4f;
    juce::ColourGradient capGradient(juce::Colour(0xFF6A6A6A), centreX - capRadius, centreY - capRadius,
                                     juce::Colour(0xFF2A2A2A), centreX + capRadius, centreY + capRadius, false);
    g.setGradientFill(capGradient);
    g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);
    
    // Position indicator (notch/line) with high contrast
    juce::Path pointer;
    pointer.addRectangle(-3.0f, -radius + 6, 6.0f, radius * 0.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    
    // White pointer with black outline for visibility on all backgrounds
    g.setColour(juce::Colour(0xFF000000));
    g.strokePath(pointer, juce::PathStrokeType(1.5f));
    g.setColour(juce::Colour(0xFFFFFFFF));
    g.fillPath(pointer);
    
    // Tick marks around knob
    auto numTicks = 11;
    for (int i = 0; i < numTicks; ++i)
    {
        auto tickAngle = rotaryStartAngle + (i / (float)(numTicks - 1)) * (rotaryEndAngle - rotaryStartAngle);
        auto tickLength = (i == 0 || i == numTicks - 1 || i == numTicks / 2) ? radius * 0.15f : radius * 0.1f;
        
        juce::Path tick;
        tick.addRectangle(-1.0f, -radius - 8, 2.0f, tickLength);
        tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(centreX, centreY));
        
        g.setColour(colors.text.withAlpha(0.6f));
        g.fillPath(tick);
    }
}

void AnalogLookAndFeelBase::drawVintageKnob(juce::Graphics& g, float x, float y, 
                                            float width, float height,
                                            float sliderPos, float rotaryStartAngle, 
                                            float rotaryEndAngle,
                                            juce::Slider& slider)
{
    auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
    auto centreX = x + width * 0.5f;
    auto centreY = y + height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    
    // Vintage-style shadow
    g.setColour(juce::Colour(0x40000000));
    g.fillEllipse(rx + 3, ry + 3, rw, rw);
    
    // Bakelite-style knob body
    juce::ColourGradient bodyGradient(colors.knobBody.brighter(0.2f), centreX - radius, centreY - radius,
                                      colors.knobBody.darker(0.4f), centreX + radius, centreY + radius, true);
    g.setGradientFill(bodyGradient);
    g.fillEllipse(rx, ry, rw, rw);
    
    // Inner ring
    g.setColour(colors.knobBody.darker(0.6f));
    g.drawEllipse(rx + 4, ry + 4, rw - 8, rw - 8, 2.0f);
    
    // Chicken-head pointer style with better visibility
    juce::Path pointer;
    pointer.startNewSubPath(0, -radius + 10);
    pointer.lineTo(-7, -radius + 28);
    pointer.lineTo(7, -radius + 28);
    pointer.closeSubPath();
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    
    // Black pointer with white outline for vintage look
    g.setColour(juce::Colour(0xFFFFFFFF));
    g.strokePath(pointer, juce::PathStrokeType(2.0f));
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillPath(pointer);
    
    // Center screw detail
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillEllipse(centreX - 3, centreY - 3, 6, 6);
    g.setColour(juce::Colour(0xFF4A4A4A));
    g.drawLine(centreX - 2, centreY, centreX + 2, centreY, 1.0f);
    g.drawLine(centreX, centreY - 2, centreX, centreY + 2, 1.0f);
}

juce::Label* AnalogLookAndFeelBase::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);

    // Style the text box with better contrast
    label->setColour(juce::Label::textColourId, juce::Colours::white);
    label->setColour(juce::Label::backgroundColourId, juce::Colour(0x40000000));  // Semi-transparent dark background
    label->setColour(juce::Label::outlineColourId, juce::Colour(0x30FFFFFF));     // Subtle light outline
    label->setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));

    return label;
}

void AnalogLookAndFeelBase::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    // Draw subtle rounded background for text entry
    g.setColour(juce::Colour(0x50000000));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 3.0f);
}

void AnalogLookAndFeelBase::drawIlluminatedToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown,
                                                        const juce::Colour& onGlowTop, const juce::Colour& onGlowBottom,
                                                        const juce::Colour& onTextColor,
                                                        const juce::Colour& offGradientTop, const juce::Colour& offGradientBottom,
                                                        const juce::Colour& offTextColor,
                                                        const juce::Colour& bezelColor)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    bool isOn = button.getToggleState();

    // Outer bezel
    g.setColour(bezelColor);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Button face
    auto innerBounds = bounds.reduced(2.0f);
    if (isOn)
    {
        // Illuminated gradient when ON
        juce::ColourGradient glow(onGlowTop, innerBounds.getCentreX(), innerBounds.getY(),
                                  onGlowBottom, innerBounds.getCentreX(), innerBounds.getBottom(), false);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(innerBounds, 3.0f);

        // Glow effect using lighter shade of onGlowTop
        g.setColour(onGlowTop.withAlpha(0.2f));
        g.fillRoundedRectangle(bounds.expanded(1), 5.0f);

        // Text color for lit button
        g.setColour(onTextColor);
    }
    else
    {
        // Dark recessed button when OFF
        juce::ColourGradient dark(offGradientTop, innerBounds.getCentreX(), innerBounds.getY(),
                                  offGradientBottom, innerBounds.getCentreX(), innerBounds.getBottom(), false);
        g.setGradientFill(dark);
        g.fillRoundedRectangle(innerBounds, 3.0f);

        // Inner shadow
        g.setColour(offGradientBottom.darker(0.3f));
        g.drawRoundedRectangle(innerBounds.reduced(1), 2.0f, 1.0f);

        // Top highlight for 3D effect
        g.setColour(juce::Colour(0x20FFFFFF));
        g.drawLine(innerBounds.getX() + 4, innerBounds.getY() + 2,
                   innerBounds.getRight() - 4, innerBounds.getY() + 2, 1.0f);

        // Text color for dark button
        g.setColour(offTextColor);
    }

    // Highlight/press state
    if (shouldDrawButtonAsDown)
    {
        g.setColour(juce::Colour(0x20000000));
        g.fillRoundedRectangle(innerBounds, 3.0f);
    }
    else if (shouldDrawButtonAsHighlighted && !isOn)
    {
        g.setColour(juce::Colour(0x10FFFFFF));
        g.fillRoundedRectangle(innerBounds, 3.0f);
    }

    // Draw label centered in button
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.drawText(button.getButtonText(), innerBounds, juce::Justification::centred);
}

//==============================================================================
// Vintage Opto Style
OptoLookAndFeel::OptoLookAndFeel()
{
    colors.background = juce::Colour(0xFFF5E6D3);  // Warm cream
    colors.panel = juce::Colour(0xFFE8D4B8);       // Light tan
    colors.knobBody = juce::Colour(0xFF8B7355);    // Brown bakelite
    colors.knobPointer = juce::Colour(0xFFFFFFE0); // Cream pointer
    colors.text = juce::Colour(0xFF2C1810);        // Dark brown
    colors.textDim = juce::Colour(0xFF5C4838);     // Medium brown
    colors.accent = juce::Colour(0xFFCC3333);      // Vintage red
    colors.shadow = juce::Colour(0xFF1A1410);
}

void OptoLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider)
{
    // Use metallic knob for consistency with other modes
    drawMetallicKnob(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

void OptoLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // Opto-style illuminated push button - warm amber theme
    drawIlluminatedToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown,
        juce::Colour(0xFFFFAA00), juce::Colour(0xFFCC7700), juce::Colour(0xFF2A1500),  // ON colors
        juce::Colour(0xFF5A5040), juce::Colour(0xFF3A3020), juce::Colour(0xFFE8D5B7),  // OFF colors
        juce::Colour(0xFF2A2420));  // bezel
}
//==============================================================================
// Vintage FET Style
FETLookAndFeel::FETLookAndFeel()
{
    colors.background = juce::Colour(0xFF1A1A1A);  // Black face
    colors.panel = juce::Colour(0xFF2A2A2A);       // Dark gray
    colors.knobBody = juce::Colour(0xFF4A4A4A);    // Medium gray metal
    colors.knobPointer = juce::Colour(0xFFFFFFFF); // White pointer
    colors.text = juce::Colour(0xFFE0E0E0);        // Light gray
    colors.textDim = juce::Colour(0xFF808080);     // Medium gray
    colors.accent = juce::Colour(0xFF4A9EFF);      // Blue accent
    colors.shadow = juce::Colour(0xFF000000);
}

void FETLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                      juce::Slider& slider)
{
    drawMetallicKnob(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

void FETLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                         const juce::Colour& backgroundColour,
                                         bool shouldDrawButtonAsHighlighted, 
                                         bool shouldDrawButtonAsDown)
{
    // FET-style rectangular button
    auto bounds = button.getLocalBounds().toFloat().reduced(2);
    
    // Button shadow
    g.setColour(colors.shadow.withAlpha(0.5f));
    g.fillRoundedRectangle(bounds.translated(1, 1), 2.0f);
    
    // Button body
    auto buttonColor = button.getToggleState() ? colors.accent : colors.panel;
    if (shouldDrawButtonAsDown)
        buttonColor = buttonColor.darker(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        buttonColor = buttonColor.brighter(0.1f);
    
    g.setColour(buttonColor);
    g.fillRoundedRectangle(bounds, 2.0f);
    
    // Button border
    g.setColour(colors.text.withAlpha(0.3f));
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
}

void FETLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                      bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // FET-style illuminated push button - amber/orange theme
    drawIlluminatedToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown,
        juce::Colour(0xFFFFAA00), juce::Colour(0xFFCC6600), juce::Colour(0xFF1A0A00),  // ON colors
        juce::Colour(0xFF3A3A3A), juce::Colour(0xFF252525), juce::Colour(0xFFCCCCCC),  // OFF colors
        juce::Colour(0xFF0A0A0A));  // bezel
}

//==============================================================================
// Studio FET Style (teal/cyan accent for cleaner, more modern look)
StudioFETLookAndFeel::StudioFETLookAndFeel()
{
    colors.background = juce::Colour(0xFF1A1A1A);  // Black face (same as Vintage FET)
    colors.panel = juce::Colour(0xFF2A2A2A);       // Dark gray
    colors.knobBody = juce::Colour(0xFF4A4A4A);    // Medium gray metal
    colors.knobPointer = juce::Colour(0xFFFFFFFF); // White pointer
    colors.text = juce::Colour(0xFFE0E0E0);        // Light gray
    colors.textDim = juce::Colour(0xFF808080);     // Medium gray
    colors.accent = juce::Colour(0xFF00CED1);      // Dark cyan/teal accent
    colors.shadow = juce::Colour(0xFF000000);
}

void StudioFETLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                            juce::Slider& slider)
{
    drawMetallicKnob(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

void StudioFETLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    // Studio FET-style rectangular button (similar to Vintage FET but teal accent)
    auto bounds = button.getLocalBounds().toFloat().reduced(2);

    // Button shadow
    g.setColour(colors.shadow.withAlpha(0.5f));
    g.fillRoundedRectangle(bounds.translated(1, 1), 2.0f);

    // Button body - use teal accent when toggled
    auto buttonColor = button.getToggleState() ? colors.accent : colors.panel;
    if (shouldDrawButtonAsDown)
        buttonColor = buttonColor.darker(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        buttonColor = buttonColor.brighter(0.1f);

    g.setColour(buttonColor);
    g.fillRoundedRectangle(bounds, 2.0f);

    // Button border
    g.setColour(colors.text.withAlpha(0.3f));
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
}

void StudioFETLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // Studio FET-style illuminated push button - teal/cyan theme
    drawIlluminatedToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown,
        juce::Colour(0xFF00E5E5), juce::Colour(0xFF00A5A5), juce::Colour(0xFF001515),  // ON colors (teal)
        juce::Colour(0xFF3A3A3A), juce::Colour(0xFF252525), juce::Colour(0xFFCCCCCC),  // OFF colors
        juce::Colour(0xFF0A0A0A));  // bezel
}

//==============================================================================
// Classic VCA Style
VCALookAndFeel::VCALookAndFeel()
{
    colors.background = juce::Colour(0xFFD4C4B0);  // Beige
    colors.panel = juce::Colour(0xFFC8B898);       // Light brown
    colors.knobBody = juce::Colour(0xFF5A5A5A);    // Dark gray metal
    colors.knobPointer = juce::Colour(0xFFFF6600); // Orange pointer
    colors.text = juce::Colour(0xFF2A2A2A);        // Dark gray
    colors.textDim = juce::Colour(0xFF6A6A6A);     // Medium gray
    colors.accent = juce::Colour(0xFFFF6600);      // Orange
    colors.shadow = juce::Colour(0xFF3A3020);
}

void VCALookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                      juce::Slider& slider)
{
    drawMetallicKnob(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

void VCALookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                      bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // VCA-style illuminated push button - warm orange theme
    drawIlluminatedToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown,
        juce::Colour(0xFFFF8800), juce::Colour(0xFFCC5500), juce::Colour(0xFF1A0A00),  // ON colors
        juce::Colour(0xFF4A4A4A), juce::Colour(0xFF2A2A2A), juce::Colour(0xFFDDDDDD),  // OFF colors
        juce::Colour(0xFF1A1A1A));  // bezel
}

//==============================================================================
// Bus Compressor Style
BusLookAndFeel::BusLookAndFeel()
{
    colors.background = juce::Colour(0xFF2C3E50);  // Dark blue-gray
    colors.panel = juce::Colour(0xFF34495E);       // Slightly lighter
    colors.knobBody = juce::Colour(0xFF5A6C7D);    // Blue-gray metal
    colors.knobPointer = juce::Colour(0xFFFFFFFF); // White pointer for visibility
    colors.text = juce::Colour(0xFFECF0F1);        // Off-white
    colors.textDim = juce::Colour(0xFF95A5A6);     // Light gray
    colors.accent = juce::Colour(0xFF4A9EFF);      // Blue accent to match theme
    colors.shadow = juce::Colour(0xFF1A252F);
}

void BusLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                      juce::Slider& slider)
{
    drawMetallicKnob(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

void BusLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                  int, int, int, int,
                                  juce::ComboBox& box)
{
    // Bus-style selector
    auto bounds = juce::Rectangle<float>(0, 0, width, height);
    
    // Background
    g.setColour(colors.panel);
    g.fillRoundedRectangle(bounds, 3.0f);
    
    // Inset shadow
    g.setColour(colors.shadow.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.reduced(1), 3.0f, 1.0f);
    
    // Selected state highlight
    if (isButtonDown)
    {
        g.setColour(colors.accent.withAlpha(0.2f));
        g.fillRoundedRectangle(bounds, 3.0f);
    }
    
    // Border
    g.setColour(colors.text.withAlpha(0.3f));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    
    // Down arrow
    juce::Path arrow;
    arrow.addTriangle(width - 18, height * 0.4f,
                     width - 10, height * 0.6f,
                     width - 26, height * 0.6f);
    g.setColour(colors.text);
    g.fillPath(arrow);
}
void BusLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                      bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // Bus-style illuminated push button - professional console look
    drawIlluminatedToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown,
        juce::Colour(0xFF4488CC), juce::Colour(0xFF2266AA), juce::Colour(0xFFFFFFFF),  // ON colors
        juce::Colour(0xFF3A4550), juce::Colour(0xFF2A3540), juce::Colour(0xFFB0C0D0),  // OFF colors
        juce::Colour(0xFF1A2530));  // bezel
}

//==============================================================================
// Studio VCA Style (precision red)
StudioVCALookAndFeel::StudioVCALookAndFeel()
{
    colors.background = juce::Colour(0xFF2a1518);  // Dark red
    colors.panel = juce::Colour(0xFF1a0d0f);       // Darker red
    colors.knobBody = juce::Colour(0xFF4A4A4A);    // Medium gray metal (matching other modes)
    colors.knobPointer = juce::Colour(0xFFFFFFFF); // White pointer
    colors.text = juce::Colour(0xFFd0d0d0);        // Light gray
    colors.textDim = juce::Colour(0xFFa0a0a0);     // Medium gray
    colors.accent = juce::Colour(0xFFcc3333);      // Studio red
    colors.shadow = juce::Colour(0xFF0a0505);
}

void StudioVCALookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                            juce::Slider& slider)
{
    // Use the shared metallic knob - same as all other modes
    drawMetallicKnob(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

void StudioVCALookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // Studio VCA style illuminated push button - red accent theme
    drawIlluminatedToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown,
        juce::Colour(0xFFDD4444), juce::Colour(0xFFAA2222), juce::Colour(0xFFFFFFFF),  // ON colors
        juce::Colour(0xFF3A2828), juce::Colour(0xFF2A1818), juce::Colour(0xFFCCBBBB),  // OFF colors
        juce::Colour(0xFF1A0808));  // bezel
}

//==============================================================================
// Digital Style
DigitalLookAndFeel::DigitalLookAndFeel()
{
    colors.background = juce::Colour(0xFF1A1A2E);  // Modern dark blue
    colors.panel = juce::Colour(0xFF16213E);       // Slightly lighter blue
    colors.knobBody = juce::Colour(0xFF4A4A4A);    // Medium gray metal (matching other modes)
    colors.knobPointer = juce::Colour(0xFFFFFFFF); // White pointer
    colors.text = juce::Colour(0xFFE0E0E0);        // Light gray
    colors.textDim = juce::Colour(0xFF808080);     // Medium gray
    colors.accent = juce::Colour(0xFF00D4FF);      // Cyan accent
    colors.shadow = juce::Colour(0xFF0A0A14);
}

void DigitalLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                          juce::Slider& slider)
{
    // Use the shared metallic knob for consistency
    drawMetallicKnob(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

void DigitalLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // Digital style illuminated push button - cyan accent theme
    drawIlluminatedToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown,
        juce::Colour(0xFF00DDFF), juce::Colour(0xFF00AACC), juce::Colour(0xFF001520),  // ON colors
        juce::Colour(0xFF2A2A3E), juce::Colour(0xFF1A1A2E), juce::Colour(0xFFBBCCDD),  // OFF colors
        juce::Colour(0xFF0A0A1E));  // bezel
}

//==============================================================================
// Analog VU Meter
AnalogVUMeter::AnalogVUMeter()
{
    needlePosition = 0.8f;  // Initialize needle at 0 dB rest position
    startTimerHz(60);
}

AnalogVUMeter::~AnalogVUMeter()
{
    stopTimer();
}

void AnalogVUMeter::setLevel(float newLevel)
{
    targetLevel = newLevel;
    
    // Update peak
    if (newLevel > peakLevel)
    {
        peakLevel = newLevel;
        peakHoldTime = 2.0f;
    }
}

void AnalogVUMeter::timerCallback()
{
    const float dt = 1.0f / kRefreshRateHz;

    // Map target dB to needle position (-20dB to +3dB range)
    // 0 dB = rest position (no compression), negative = gain reduction
    float displayValue = juce::jlimit(-20.0f, 3.0f, targetLevel);
    if (std::abs(displayValue) < 0.001f)
        displayValue = 0.0f;

    // Normalize to 0-1 range: -20dB = 0.0, 0dB = 0.87, +3dB = 1.0
    float targetNeedle = (displayValue + 20.0f) / 23.0f;
    targetNeedle = juce::jlimit(0.0f, 1.0f, targetNeedle);

    // Calculate asymmetric ballistics coefficients
    // Attack = fast (50ms), Release = slower (150ms) for professional GR meter feel
    float displacement = targetNeedle - needlePosition;
    bool isAttack = displacement < 0.0f;  // Needle moving left = more compression = attack

    float timeConstantMs = isAttack ? kAttackTimeMs : kReleaseTimeMs;
    float ballisticsCoeff = 1.0f - std::exp(-1000.0f * dt / timeConstantMs);

    // Damped spring physics for mechanical needle overshoot
    // This creates the authentic ~1% overshoot of real analog meters
    float springForce = displacement * kOvershootStiffness;
    float dampingForce = -needleVelocity * kOvershootDamping * 2.0f * std::sqrt(kOvershootStiffness);

    // Update velocity and position with spring physics
    float acceleration = springForce + dampingForce;
    needleVelocity += acceleration * dt;
    needlePosition += needleVelocity * dt;

    // Blend spring physics with ballistics for proper timing
    needlePosition += ballisticsCoeff * (targetNeedle - needlePosition) * 0.4f;

    // Clamp position
    needlePosition = juce::jlimit(0.0f, 1.0f, needlePosition);

    // Dampen tiny oscillations
    if (std::abs(needleVelocity) < 0.0005f && std::abs(displacement) < 0.001f)
        needleVelocity = 0.0f;

    // Peak hold decay
    if (peakHoldTime > 0)
    {
        peakHoldTime -= dt;
        if (peakHoldTime <= 0)
            peakLevel = targetLevel;
    }

    // Calculate peak needle position for display
    if (displayPeaks)
    {
        float peakDisplayValue = juce::jlimit(-20.0f, 3.0f, peakLevel);
        float peakNormalizedPos = (peakDisplayValue + 20.0f) / 23.0f;
        peakNeedlePosition = juce::jlimit(0.0f, 1.0f, peakNormalizedPos);
    }

    repaint();
}

void AnalogVUMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    
    // Calculate scale factor based on component size
    float scaleFactor = juce::jmin(bounds.getWidth() / 400.0f, bounds.getHeight() / 250.0f);
    scaleFactor = juce::jmax(0.5f, scaleFactor);  // Minimum scale to keep things readable
    
    // Draw outer gray frame - thinner bezel
    g.setColour(juce::Colour(0xFFB4B4B4));  // Light gray frame
    g.fillRoundedRectangle(bounds, 3.0f * scaleFactor);
    
    // Draw inner darker frame - thinner
    auto innerFrame = bounds.reduced(2.0f * scaleFactor);
    g.setColour(juce::Colour(0xFF3A3A3A));  // Dark gray/black inner frame
    g.fillRoundedRectangle(innerFrame, 2.0f * scaleFactor);
    
    // Draw classic VU meter face with warm cream color
    auto faceBounds = innerFrame.reduced(3.0f * scaleFactor);
    // Classic VU meter cream/beige color like vintage meters
    g.setColour(juce::Colour(0xFFF8F4E6));  // Warm cream color
    g.fillRoundedRectangle(faceBounds, 2.0f * scaleFactor);
    
    // IMPORTANT: Set clipping region to ensure nothing draws outside the face bounds
    g.saveState();
    g.reduceClipRegion(faceBounds.toNearestInt());
    
    // Set up meter geometry - calculate to fit within faceBounds
    auto centreX = faceBounds.getCentreX();
    // Pivot must be positioned so the arc and text stay within faceBounds
    auto pivotY = faceBounds.getBottom() - (3 * scaleFactor);  // Keep pivot very close to bottom
    
    // Calculate needle length that keeps the arc and text within bounds
    // With thinner bezel, we can use more of the available space
    auto maxHeightForText = faceBounds.getHeight() * 0.88f;  // Use more height now
    auto maxWidthRadius = faceBounds.getWidth() * 0.49f;  // Use more width
    auto needleLength = juce::jmin(maxWidthRadius, maxHeightForText);
    
    // VU scale (-20 to +3 dB) with classic VU meter arc
    const float pi = 3.14159265f;
    // Classic VU meter angles - wider sweep for authentic look
    const float scaleStart = -2.7f;  // Start angle (left) - wider
    const float scaleEnd = -0.44f;   // End angle (right) - wider
    
    // Draw scale arc (more visible)
    g.setColour(juce::Colour(0xFF1A1A1A).withAlpha(0.7f));
    juce::Path scaleArc;
    scaleArc.addCentredArc(centreX, pivotY, needleLength * 0.95f, needleLength * 0.95f, 0, scaleStart, scaleEnd, true);
    g.strokePath(scaleArc, juce::PathStrokeType(2.0f * scaleFactor));
    
    // Font setup for scale markings
    float baseFontSize = juce::jmax(10.0f, 14.0f * scaleFactor);
    g.setFont(juce::Font(juce::FontOptions(baseFontSize)));
    
    // Top scale - VU markings (-20 to +3)
    const float dbValues[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    const int numDbValues = 11;
    
    for (int i = 0; i < numDbValues; ++i)
    {
        float db = dbValues[i];
        float normalizedPos = (db + 20.0f) / 23.0f;  // Range is -20 to +3
        float angle = scaleStart + normalizedPos * (scaleEnd - scaleStart);
        
        // Determine if this is a major marking
        bool isMajor = (db == -20 || db == -10 || db == -7 || db == -5 || db == -3 || db == -2 || db == -1 || db == 0 || db == 1 || db == 3);
        bool showText = (db == -20 || db == -10 || db == -7 || db == -5 || db == -3 || db == -2 || db == -1 || db == 0);  // Show all negative values and 0
        
        // Draw tick marks for all values
        auto tickLength = isMajor ? (10.0f * scaleFactor) : (6.0f * scaleFactor);
        auto tickRadius = needleLength * 0.95f;  // Position ticks at the arc
        auto x1 = centreX + tickRadius * std::cos(angle);
        auto y1 = pivotY + tickRadius * std::sin(angle);
        auto x2 = centreX + (tickRadius + tickLength) * std::cos(angle);
        auto y2 = pivotY + (tickRadius + tickLength) * std::sin(angle);
        
        // Classic VU meter colors - red zone starts at 0
        if (db >= 0)
            g.setColour(juce::Colour(0xFFD42C2C));  // Classic VU red for 0 and above
        else
            g.setColour(juce::Colour(0xFF2A2A2A));  // Dark gray/black for negative
        
        g.drawLine(x1, y1, x2, y2, isMajor ? 2.0f * scaleFactor : 1.0f * scaleFactor);
        
        // Draw text labels for major markings
        if (showText)
        {
            // Position text inside the arc, ensuring it stays within bounds
            auto textRadius = needleLength * 0.72f;  // Position well inside to avoid top clipping
            auto textX = centreX + textRadius * std::cos(angle);
            auto textY = pivotY + textRadius * std::sin(angle);
            
            // Text boxes sized appropriately
            float textBoxWidth = 30 * scaleFactor;
            float textBoxHeight = 15 * scaleFactor;
            
            // Ensure text doesn't go above the face bounds
            float minY = faceBounds.getY() + (5 * scaleFactor);
            if (textY - textBoxHeight/2 < minY)
                textY = minY + textBoxHeight/2;
            
            juce::String dbText;
            if (db == 0)
                dbText = "0";
            else if (db > 0)
                dbText = "+" + juce::String((int)db);
            else
                dbText = juce::String((int)db);
            
            // Classic VU meter text colors - red zone at 0 and above
            if (db >= 0)
                g.setColour(juce::Colour(0xFFD42C2C));  // Red for 0 and above
            else
                g.setColour(juce::Colour(0xFF2A2A2A));  // Dark for negative
            
            g.drawText(dbText, textX - textBoxWidth/2, textY - textBoxHeight/2, 
                      textBoxWidth, textBoxHeight, juce::Justification::centred);
        }
    }
    
    // Bottom scale - percentage markings (0, 50, 100%)
    float percentFontSize = juce::jmax(7.0f, 9.0f * scaleFactor);
    g.setFont(juce::Font(juce::FontOptions(percentFontSize)));
    g.setColour(juce::Colour(0xFF606060));
    
    // Draw 0 and 100% marks only (50% clutters the display)
    const int percentValues[] = {0, 100};
    for (int percent : percentValues)
    {
        // Map percentage to position on scale (adjusted for -20 to +3 range)
        float dbEquiv = -20.0f + (percent / 100.0f) * 23.0f;
        float normalizedPos = (dbEquiv + 20.0f) / 23.0f;
        float angle = scaleStart + normalizedPos * (scaleEnd - scaleStart);
        
        auto textRadius = needleLength * 1.15f;  // Position below the arc
        auto textX = centreX + textRadius * std::cos(angle);
        auto textY = pivotY + textRadius * std::sin(angle) + (5 * scaleFactor);  // Push down
        
        // No need to adjust edge labels with clipping in place
        
        float textBoxWidth = 30 * scaleFactor;
        float textBoxHeight = 10 * scaleFactor;
        
        juce::String percentText = juce::String(percent) + "%";
        g.drawText(percentText, textX - textBoxWidth/2, textY - textBoxHeight/2, 
                  textBoxWidth, textBoxHeight, juce::Justification::centred);
    }
    
    // Draw VU text in classic position
    g.setColour(juce::Colour(0xFF2A2A2A));
    float vuFontSize = juce::jmax(18.0f, 24.0f * scaleFactor);
    g.setFont(juce::Font(juce::FontOptions(vuFontSize)).withTypefaceStyle("Regular"));
    // Position VU text above the needle pivot like classic meters
    float vuY = pivotY - (needleLength * 0.4f);
    g.drawText("VU", centreX - 20 * scaleFactor, vuY, 
              40 * scaleFactor, 20 * scaleFactor, juce::Justification::centred);
    
    // Draw needle
    float needleAngle = scaleStart + needlePosition * (scaleEnd - scaleStart);

    // Classic VU meter needle - thin black line like vintage meters
    g.setColour(juce::Colour(0xFF000000));
    juce::Path needle;
    needle.startNewSubPath(centreX, pivotY);
    needle.lineTo(centreX + needleLength * 0.96f * std::cos(needleAngle),
                 pivotY + needleLength * 0.96f * std::sin(needleAngle));
    g.strokePath(needle, juce::PathStrokeType(1.5f * scaleFactor));  // Thin needle like classic VU

    // Draw peak hold indicator - small red triangle at peak position
    if (displayPeaks && peakNeedlePosition > needlePosition + 0.02f)  // Only show if peak is significantly above current
    {
        float peakAngle = scaleStart + peakNeedlePosition * (scaleEnd - scaleStart);
        float peakRadius = needleLength * 0.92f;  // Position on arc

        // Small red triangle/marker at peak position
        float peakX = centreX + peakRadius * std::cos(peakAngle);
        float peakY = pivotY + peakRadius * std::sin(peakAngle);

        // Draw a small red dot/marker
        float markerSize = 4.0f * scaleFactor;
        g.setColour(juce::Colour(0xFFFF3333));  // Bright red
        g.fillEllipse(peakX - markerSize/2, peakY - markerSize/2, markerSize, markerSize);

        // Optional: Draw a thin line from pivot to peak marker
        g.setColour(juce::Colour(0x60FF3333));  // Semi-transparent red
        juce::Path peakLine;
        peakLine.startNewSubPath(centreX, pivotY);
        peakLine.lineTo(peakX, peakY);
        g.strokePath(peakLine, juce::PathStrokeType(0.5f * scaleFactor));
    }

    // Classic needle pivot - small simple black dot
    float pivotRadius = 3 * scaleFactor;
    g.setColour(juce::Colour(0xFF000000));
    g.fillEllipse(centreX - pivotRadius, pivotY - pivotRadius, pivotRadius * 2, pivotRadius * 2);
    
    // Restore graphics state to remove clipping
    g.restoreState();
    
    // Subtle glass reflection effect (drawn after restoring state, so it's on top)
    auto glassBounds = innerFrame.reduced(1 * scaleFactor);
    auto highlightBounds = glassBounds.removeFromTop(glassBounds.getHeight() * 0.2f).reduced(10 * scaleFactor, 5 * scaleFactor);
    juce::ColourGradient highlightGradient(
        juce::Colour(0x20FFFFFF), 
        highlightBounds.getCentreX(), highlightBounds.getY(),
        juce::Colour(0x00FFFFFF), 
        highlightBounds.getCentreX(), highlightBounds.getBottom(), 
        false
    );
    g.setGradientFill(highlightGradient);
    g.fillRoundedRectangle(highlightBounds, 3.0f * scaleFactor);
}

//==============================================================================
// GR History Graph
GRHistoryGraph::GRHistoryGraph()
{
    grHistory.fill(0.0f);
}

void GRHistoryGraph::updateHistory(const UniversalCompressor& processor)
{
    // Note: Both updateHistory and paint run on the message thread (timer callback
    // and paint are both message-thread operations in JUCE), so no synchronization
    // is needed within this component. The processor's grHistory array now uses
    // atomic<float> elements for thread-safe reads from the audio thread.

    // Copy from processor's atomic array to local array
    for (size_t i = 0; i < grHistory.size(); ++i)
        grHistory[i] = processor.getGRHistoryValue(static_cast<int>(i));

    // Validate writePos bounds to prevent out-of-range access in paint()
    historyWritePos = juce::jlimit(0, static_cast<int>(grHistory.size()) - 1,
                                    processor.getGRHistoryWritePos());
    repaint();
}

void GRHistoryGraph::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Calculate scale factor for HiDPI
    float scaleFactor = juce::jmin(bounds.getWidth() / 400.0f, bounds.getHeight() / 250.0f);
    scaleFactor = juce::jmax(0.5f, scaleFactor);

    // Draw outer frame - professional look
    g.setColour(juce::Colour(0xFF606060));
    g.fillRoundedRectangle(bounds, 4.0f * scaleFactor);

    // Draw inner frame with subtle gradient
    auto innerFrame = bounds.reduced(2.0f * scaleFactor);
    juce::ColourGradient bgGradient(
        juce::Colour(0xFF1A1A1E), innerFrame.getX(), innerFrame.getY(),
        juce::Colour(0xFF101014), innerFrame.getX(), innerFrame.getBottom(),
        false);
    g.setGradientFill(bgGradient);
    g.fillRoundedRectangle(innerFrame, 3.0f * scaleFactor);

    // Graph area with margin for labels
    auto graphBounds = innerFrame.reduced(10.0f * scaleFactor);
    graphBounds.removeFromLeft(28.0f * scaleFactor);  // Space for dB labels
    graphBounds.removeFromBottom(14.0f * scaleFactor); // Space for time labels

    // Draw horizontal grid lines at each dB level
    const float dbValues[] = {0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f};
    const int numDbLines = 6;
    for (int i = 0; i < numDbLines; ++i)
    {
        float normalizedY = -dbValues[i] / 30.0f;
        float y = graphBounds.getY() + normalizedY * graphBounds.getHeight();

        // Lighter line at 0dB, dimmer for others
        if (i == 0)
            g.setColour(juce::Colour(0xFF505050));  // 0dB line more visible
        else
            g.setColour(juce::Colour(0xFF2A2A2E));

        g.drawHorizontalLine(static_cast<int>(y), graphBounds.getX(), graphBounds.getRight());
    }

    // Draw vertical grid lines for time reference (1 sec intervals)
    g.setColour(juce::Colour(0xFF2A2A2E));
    for (int i = 1; i < 4; ++i)  // 1, 2, 3 second marks
    {
        float x = graphBounds.getX() + (graphBounds.getWidth() * i / 4.0f);
        g.drawVerticalLine(static_cast<int>(x), graphBounds.getY(), graphBounds.getBottom());
    }

    // Draw dB scale on left
    float fontSize = juce::jmax(8.0f, 9.0f * scaleFactor);
    g.setFont(juce::Font(juce::FontOptions(fontSize)));

    for (int i = 0; i < numDbLines; ++i)
    {
        float normalizedY = -dbValues[i] / 30.0f;
        float y = graphBounds.getY() + normalizedY * graphBounds.getHeight();

        // Color: brighter for 0dB
        if (dbValues[i] == 0.0f)
            g.setColour(juce::Colour(0xFFAAAAAA));
        else
            g.setColour(juce::Colour(0xFF707070));

        g.drawText(juce::String(static_cast<int>(dbValues[i])),
                   innerFrame.getX() + 2 * scaleFactor, y - 6 * scaleFactor,
                   24 * scaleFactor, 12 * scaleFactor,
                   juce::Justification::right);
    }

    // Find peak GR for indicator
    float peakGR = 0.0f;
    const int historySize = 128;
    for (int i = 0; i < historySize; ++i)
    {
        if (grHistory[i] < peakGR)
            peakGR = grHistory[i];
    }

    // Draw GR history as filled path
    juce::Path grPath;
    float xStep = graphBounds.getWidth() / static_cast<float>(historySize - 1);

    // Start path at top left (0 GR)
    grPath.startNewSubPath(graphBounds.getX(), graphBounds.getY());

    for (int i = 0; i < historySize; ++i)
    {
        // Read from circular buffer, oldest first
        int idx = (historyWritePos + i) % historySize;
        float gr = grHistory[idx];  // GR in dB (negative values)

        // Map GR to y position: 0dB at top, -30dB at bottom
        float normalizedGR = juce::jlimit(0.0f, 1.0f, -gr / 30.0f);
        float y = graphBounds.getY() + normalizedGR * graphBounds.getHeight();
        float x = graphBounds.getX() + i * xStep;

        grPath.lineTo(x, y);
    }

    // Close path back to top right
    grPath.lineTo(graphBounds.getRight(), graphBounds.getY());
    grPath.closeSubPath();

    // Fill with professional gradient - green to darker green
    juce::ColourGradient grGradient(
        juce::Colour(0xFF00CC77).withAlpha(0.9f), graphBounds.getX(), graphBounds.getY(),
        juce::Colour(0xFF003322).withAlpha(0.7f), graphBounds.getX(), graphBounds.getBottom(),
        false);
    g.setGradientFill(grGradient);
    g.fillPath(grPath);

    // Draw bright outline for current GR trace
    juce::Path outlinePath;
    for (int i = 0; i < historySize; ++i)
    {
        int idx = (historyWritePos + i) % historySize;
        float gr = grHistory[idx];
        float normalizedGR = juce::jlimit(0.0f, 1.0f, -gr / 30.0f);
        float y = graphBounds.getY() + normalizedGR * graphBounds.getHeight();
        float x = graphBounds.getX() + i * xStep;

        if (i == 0)
            outlinePath.startNewSubPath(x, y);
        else
            outlinePath.lineTo(x, y);
    }

    // Glow effect on outline
    g.setColour(juce::Colour(0x3000FF88));
    g.strokePath(outlinePath, juce::PathStrokeType(4.0f * scaleFactor));
    g.setColour(juce::Colour(0xFF00FF88));
    g.strokePath(outlinePath, juce::PathStrokeType(1.5f * scaleFactor));

    // Draw "NOW" marker on right side
    float nowX = graphBounds.getRight();
    g.setColour(juce::Colour(0xFFFFAA00));
    g.drawVerticalLine(static_cast<int>(nowX), graphBounds.getY(), graphBounds.getBottom());

    // Peak GR indicator line (horizontal dotted line at peak)
    if (peakGR < -0.5f)  // Only show if there's meaningful GR
    {
        float peakY = graphBounds.getY() + (-peakGR / 30.0f) * graphBounds.getHeight();
        g.setColour(juce::Colour(0x80FF6666));

        // Draw peak line
        g.drawHorizontalLine(static_cast<int>(peakY), graphBounds.getX(), graphBounds.getRight());

        // Peak value label
        g.setColour(juce::Colour(0xFFFF6666));
        g.setFont(juce::Font(juce::FontOptions(fontSize).withStyle("Bold")));
        juce::String peakText = juce::String(peakGR, 1) + "dB";
        g.drawText(peakText, graphBounds.getRight() - 40 * scaleFactor, peakY - 12 * scaleFactor,
                   38 * scaleFactor, 12 * scaleFactor, juce::Justification::right);
    }

    // Time labels at bottom
    g.setColour(juce::Colour(0xFF707070));
    g.setFont(juce::Font(juce::FontOptions(fontSize)));
    g.drawText("-4s", graphBounds.getX(), graphBounds.getBottom() + 2 * scaleFactor,
               20 * scaleFactor, 12 * scaleFactor, juce::Justification::left);
    g.drawText("-2s", graphBounds.getCentreX() - 10 * scaleFactor, graphBounds.getBottom() + 2 * scaleFactor,
               20 * scaleFactor, 12 * scaleFactor, juce::Justification::centred);
    g.drawText("now", graphBounds.getRight() - 20 * scaleFactor, graphBounds.getBottom() + 2 * scaleFactor,
               20 * scaleFactor, 12 * scaleFactor, juce::Justification::right);

    // Title with background for visibility
    float titleFontSize = juce::jmax(11.0f, 14.0f * scaleFactor);
    auto titleBounds = juce::Rectangle<float>(
        graphBounds.getX() + graphBounds.getWidth() * 0.2f,
        graphBounds.getY() + 4 * scaleFactor,
        graphBounds.getWidth() * 0.6f,
        18 * scaleFactor);

    // Dark background behind title for contrast
    g.setColour(juce::Colour(0xDD1A1A1A));
    g.fillRoundedRectangle(titleBounds, 3.0f);

    // Title text in bright color
    g.setColour(juce::Colour(0xFFFFFFFF));
    g.setFont(juce::Font(juce::FontOptions(titleFontSize).withStyle("Bold")));
    g.drawText("GR HISTORY", titleBounds, juce::Justification::centred);

    // Time label
    g.setColour(juce::Colour(0xFF808080));
    g.setFont(juce::Font(juce::FontOptions(fontSize)));
    g.drawText("~4 sec", graphBounds.getRight() - 40 * scaleFactor, graphBounds.getBottom() + 2 * scaleFactor,
               40 * scaleFactor, 12 * scaleFactor, juce::Justification::right);
}

//==============================================================================
// VU Meter with Label (with click-to-toggle GR history)
VUMeterWithLabel::VUMeterWithLabel()
{
    vuMeter = std::make_unique<AnalogVUMeter>();
    grHistoryGraph = std::make_unique<GRHistoryGraph>();

    addAndMakeVisible(vuMeter.get());
    addChildComponent(grHistoryGraph.get());  // Hidden by default
}

void VUMeterWithLabel::setLevel(float newLevel)
{
    if (vuMeter)
        vuMeter->setLevel(newLevel);
}

void VUMeterWithLabel::setGRHistory(const UniversalCompressor& processor)
{
    if (grHistoryGraph)
        grHistoryGraph->updateHistory(processor);
}

void VUMeterWithLabel::setShowHistory(bool show)
{
    showHistory = show;
    vuMeter->setVisible(!show);
    grHistoryGraph->setVisible(show);
    repaint();
}

void VUMeterWithLabel::mouseDown(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    setShowHistory(!showHistory);
}

void VUMeterWithLabel::resized()
{
    auto bounds = getLocalBounds();

    // Reserve space for label at bottom
    auto labelHeight = juce::jmin(30, bounds.getHeight() / 8);
    auto meterBounds = bounds.removeFromTop(bounds.getHeight() - labelHeight);

    if (vuMeter)
        vuMeter->setBounds(meterBounds);
    if (grHistoryGraph)
        grHistoryGraph->setBounds(meterBounds);
}

void VUMeterWithLabel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Calculate scale factor based on component size
    float scaleFactor = juce::jmin(bounds.getWidth() / 400.0f, bounds.getHeight() / 280.0f);
    scaleFactor = juce::jmax(0.5f, scaleFactor);

    // Draw label at bottom
    auto labelHeight = juce::jmin(30.0f, bounds.getHeight() / 8.0f);
    auto labelArea = bounds.removeFromBottom(static_cast<int>(labelHeight));

    // Draw a subtle background behind the label for better visibility
    g.setColour(juce::Colour(0x30000000));
    g.fillRoundedRectangle(labelArea.toFloat().reduced(2.0f), 3.0f);

    // Use brighter, more visible text color with slight glow effect
    float fontSize = juce::jmax(11.0f, 14.0f * scaleFactor);
    g.setFont(juce::Font(juce::FontOptions(fontSize).withStyle("Bold")));

    // Show different label based on mode - bright orange accent for visibility
    juce::String labelText = showHistory ? "GR HISTORY (click)" : "LEVEL (click)";

    // Draw subtle text shadow for depth
    g.setColour(juce::Colour(0x40000000));
    g.drawText(labelText, labelArea.translated(1, 1), juce::Justification::centred);

    // Draw main text in bright orange/amber for high visibility
    g.setColour(juce::Colour(0xFFE09040));  // Warm amber color
    g.drawText(labelText, labelArea, juce::Justification::centred);
}

//==============================================================================
// Release Time Indicator
ReleaseTimeIndicator::ReleaseTimeIndicator()
{
}

void ReleaseTimeIndicator::setReleaseTime(float timeMs)
{
    currentReleaseMs = timeMs;
    repaint();
}

void ReleaseTimeIndicator::setTargetRelease(float timeMs)
{
    targetReleaseMs = timeMs;
    repaint();
}

void ReleaseTimeIndicator::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    // Background
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(bounds, 3.0f);

    // Border
    g.setColour(juce::Colour(0xFF3A3A3A));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // Calculate bar width based on release time ratio
    // Map release time: 1ms to 5000ms (log scale)
    auto mapToNormalized = [](float ms) {
        float minLog = std::log10(1.0f);
        float maxLog = std::log10(5000.0f);
        float valueLog = std::log10(juce::jmax(1.0f, ms));
        return juce::jlimit(0.0f, 1.0f, (valueLog - minLog) / (maxLog - minLog));
    };

    float currentNorm = mapToNormalized(currentReleaseMs);
    float targetNorm = mapToNormalized(targetReleaseMs);

    auto barBounds = bounds.reduced(4.0f);

    // Draw target position marker (thin line)
    float targetX = barBounds.getX() + targetNorm * barBounds.getWidth();
    g.setColour(juce::Colour(0xFF666666));
    g.fillRect(targetX - 1, barBounds.getY(), 2.0f, barBounds.getHeight());

    // Draw current release bar
    float barWidth = currentNorm * barBounds.getWidth();
    juce::ColourGradient barGradient(
        juce::Colour(0xFF00AAFF), barBounds.getX(), barBounds.getY(),
        juce::Colour(0xFF0066AA), barBounds.getX() + barWidth, barBounds.getY(),
        false);
    g.setGradientFill(barGradient);
    g.fillRoundedRectangle(barBounds.getX(), barBounds.getY(), barWidth, barBounds.getHeight(), 2.0f);

    // Text overlay showing actual release time
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));

    juce::String timeText;
    if (currentReleaseMs >= 1000.0f)
        timeText = juce::String(currentReleaseMs / 1000.0f, 2) + "s";
    else
        timeText = juce::String(static_cast<int>(currentReleaseMs)) + "ms";

    g.drawText("Rel: " + timeText, bounds, juce::Justification::centred);
}

// NOTE: LEDMeter implementation moved to shared/LEDMeter.cpp for consistency across all plugins.

//==============================================================================
// Ratio Button Group - FET-style illuminated push buttons
RatioButtonGroup::RatioButtonGroup()
{
    setRepaintsOnMouseActivity(true);
}

RatioButtonGroup::~RatioButtonGroup()
{
}

void RatioButtonGroup::setSelectedRatio(int index)
{
    if (index >= 0 && index < ratioLabels.size())
    {
        currentRatio = index;
        repaint();
    }
}

void RatioButtonGroup::setAccentColor(juce::Colour color)
{
    accentColorBright = color;
    // Create a darker version for the gradient
    accentColorDark = color.darker(0.4f);
    repaint();
}

void RatioButtonGroup::resized()
{
    auto bounds = getLocalBounds();
    int numButtons = ratioLabels.size();
    int buttonWidth = bounds.getWidth() / numButtons;
    int buttonHeight = juce::jmin(bounds.getHeight(), 32);
    int yOffset = (bounds.getHeight() - buttonHeight) / 2;

    buttonBounds.clear();
    for (int i = 0; i < numButtons; ++i)
    {
        buttonBounds.push_back(juce::Rectangle<int>(i * buttonWidth + 2, yOffset, buttonWidth - 4, buttonHeight));
    }
}

void RatioButtonGroup::paint(juce::Graphics& g)
{
    // Ensure safe iteration - use minimum of both array sizes
    int numButtons = std::min(static_cast<int>(ratioLabels.size()), static_cast<int>(buttonBounds.size()));

    // Defensive check - log if sizes don't match (indicates resized() issue)
    jassert(ratioLabels.size() == buttonBounds.size());

    for (int i = 0; i < numButtons; ++i)
    {
        auto& bounds = buttonBounds[static_cast<size_t>(i)];
        bool isSelected = (i == currentRatio);

        // FET-style illuminated push button
        // Outer bezel
        g.setColour(juce::Colour(0xFF1A1A1A));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

        // Button face - recessed look
        auto innerBounds = bounds.reduced(2);
        if (isSelected)
        {
            // Illuminated with accent color when selected
            juce::ColourGradient glow(accentColorBright, innerBounds.getCentreX(), innerBounds.getY(),
                                      accentColorDark, innerBounds.getCentreX(), innerBounds.getBottom(), false);
            g.setGradientFill(glow);
            g.fillRoundedRectangle(innerBounds.toFloat(), 3.0f);

            // Glow effect
            g.setColour(accentColorBright.withAlpha(0.25f));
            g.fillRoundedRectangle(bounds.toFloat().expanded(2), 5.0f);

            // Text shadow for depth - darker version of accent
            g.setColour(accentColorDark.darker(0.6f));
            g.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
            g.drawText(ratioLabels[i], innerBounds.translated(1, 1), juce::Justification::centred);

            // Main text - very dark on lit button
            g.setColour(accentColorDark.darker(0.8f));
        }
        else
        {
            // Dark recessed button when not selected
            juce::ColourGradient dark(juce::Colour(0xFF3A3A3A), innerBounds.getCentreX(), innerBounds.getY(),
                                      juce::Colour(0xFF252525), innerBounds.getCentreX(), innerBounds.getBottom(), false);
            g.setGradientFill(dark);
            g.fillRoundedRectangle(innerBounds.toFloat(), 3.0f);

            // Subtle inner shadow
            g.setColour(juce::Colour(0xFF151515));
            g.drawRoundedRectangle(innerBounds.toFloat().reduced(1), 2.0f, 1.0f);

            // Light text on dark button
            g.setColour(juce::Colour(0xFFAAAAAA));
        }

        // Draw ratio label
        g.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
        g.drawText(ratioLabels[i], innerBounds, juce::Justification::centred);

        // Highlight edge on top for 3D effect
        if (!isSelected)
        {
            g.setColour(juce::Colour(0x20FFFFFF));
            g.drawLine(static_cast<float>(innerBounds.getX() + 4), static_cast<float>(innerBounds.getY() + 2),
                      static_cast<float>(innerBounds.getRight() - 4), static_cast<float>(innerBounds.getY() + 2), 1.0f);
        }
    }
}

void RatioButtonGroup::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < static_cast<int>(buttonBounds.size()); ++i)
    {
        if (buttonBounds[static_cast<size_t>(i)].contains(e.getPosition()))
        {
            if (i != currentRatio)
            {
                currentRatio = i;
                repaint();
                listeners.call(&Listener::ratioChanged, currentRatio);
            }
            break;
        }
    }
}
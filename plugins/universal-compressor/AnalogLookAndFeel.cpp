#include "AnalogLookAndFeel.h"
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

//==============================================================================
// Opto (LA-2A) Style
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
                                       bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/)
{
    // LED-style indicator matching other compressor modes but with warm amber color
    auto bounds = button.getLocalBounds().toFloat();
    auto ledSize = 20.0f;

    // LED housing with metallic appearance
    g.setColour(juce::Colour(0xFF2A2420));
    g.fillEllipse(4, (bounds.getHeight() - ledSize) / 2, ledSize, ledSize);

    // LED bezel
    g.setColour(juce::Colour(0xFF5A5040));
    g.drawEllipse(4, (bounds.getHeight() - ledSize) / 2, ledSize, ledSize, 2.0f);

    // LED light - warm amber when on, dark when off
    auto ledColor = button.getToggleState() ? juce::Colour(0xFFFFAA00) : juce::Colour(0xFF3A3020);
    if (button.getToggleState())
    {
        // Warm glow effect for ON state
        g.setColour(ledColor.withAlpha(0.4f));
        g.fillEllipse(2, (bounds.getHeight() - ledSize - 4) / 2, ledSize + 4, ledSize + 4);
    }

    g.setColour(ledColor);
    g.fillEllipse(7, (bounds.getHeight() - ledSize + 6) / 2, ledSize - 6, ledSize - 6);

    // Add highlight for 3D effect
    if (button.getToggleState())
    {
        g.setColour(juce::Colour(0xFFFFFFFF).withAlpha(0.5f));
        g.fillEllipse(9, (bounds.getHeight() - ledSize + 8) / 2, 4, 4);
    }

    // Label with warm cream color for visibility on dark brown background
    g.setColour(juce::Colour(0xFFE8D5B7));
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withTypefaceStyle("Bold"));
    g.drawText(button.getButtonText(), ledSize + 10, 0,
               bounds.getWidth() - ledSize - 10, bounds.getHeight(),
               juce::Justification::centredLeft);
}
//==============================================================================
// FET (1176) Style
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
    // 1176-style rectangular button
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
    // 1176-style illuminated rectangular button
    auto bounds = button.getLocalBounds().toFloat();
    auto switchWidth = 50.0f;
    auto switchHeight = 22.0f;

    // Button shadow
    g.setColour(colors.shadow.withAlpha(0.5f));
    g.fillRoundedRectangle(5, (bounds.getHeight() - switchHeight) / 2 + 1, switchWidth, switchHeight, 3.0f);

    // Button body - illuminated when on
    auto buttonColor = button.getToggleState() ? colors.accent : colors.panel;
    if (shouldDrawButtonAsDown)
        buttonColor = buttonColor.darker(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        buttonColor = buttonColor.brighter(0.1f);
    
    g.setColour(buttonColor);
    g.fillRoundedRectangle(4, (bounds.getHeight() - switchHeight) / 2, switchWidth, switchHeight, 3.0f);

    // Subtle highlight on top edge
    g.setColour(juce::Colour(0xFFFFFFFF).withAlpha(button.getToggleState() ? 0.3f : 0.1f));
    g.fillRoundedRectangle(4, (bounds.getHeight() - switchHeight) / 2, switchWidth, 6.0f, 3.0f);

    // Button border
    g.setColour(colors.text.withAlpha(0.4f));
    g.drawRoundedRectangle(4, (bounds.getHeight() - switchHeight) / 2, switchWidth, switchHeight, 3.0f, 1.0f);

    // Label
    g.setColour(colors.text);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withTypefaceStyle("Bold"));
    g.drawText(button.getButtonText(), switchWidth + 12, 0,
               bounds.getWidth() - switchWidth - 12, bounds.getHeight(),
               juce::Justification::centredLeft);
}
//==============================================================================
// VCA (DBX 160) Style
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
    // DBX-style LED button with improved visibility
    auto bounds = button.getLocalBounds().toFloat();
    auto ledSize = 20.0f;
    
    // LED housing with metallic appearance
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillEllipse(4, (bounds.getHeight() - ledSize) / 2, ledSize, ledSize);
    
    // LED bezel
    g.setColour(juce::Colour(0xFF4A4A4A));
    g.drawEllipse(4, (bounds.getHeight() - ledSize) / 2, ledSize, ledSize, 2.0f);
    
    // LED light with better contrast
    auto ledColor = button.getToggleState() ? juce::Colour(0xFF00FF44) : juce::Colour(0xFF2A2A2A);
    if (button.getToggleState())
    {
        // Bright glow effect for ON state
        g.setColour(ledColor.withAlpha(0.4f));
        g.fillEllipse(2, (bounds.getHeight() - ledSize - 4) / 2, ledSize + 4, ledSize + 4);
    }
    
    g.setColour(ledColor);
    g.fillEllipse(7, (bounds.getHeight() - ledSize + 6) / 2, ledSize - 6, ledSize - 6);
    
    // Add highlight for 3D effect
    if (button.getToggleState())
    {
        g.setColour(juce::Colour(0xFFFFFFFF).withAlpha(0.5f));
        g.fillEllipse(9, (bounds.getHeight() - ledSize + 8) / 2, 4, 4);
    }
    
    // Label with better contrast (light text on dark background)
    g.setColour(juce::Colour(0xFFDFE6E9));
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withTypefaceStyle("Bold"));
    g.drawText(button.getButtonText(), ledSize + 10, 0,
               bounds.getWidth() - ledSize - 10, bounds.getHeight(),
               juce::Justification::centredLeft);
}

//==============================================================================
// Bus (SSL G) Style
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
    // SSL-style selector
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
    // SSL-style illuminated toggle with indicator light
    auto bounds = button.getLocalBounds().toFloat();
    auto switchWidth = 50.0f;
    auto switchHeight = 22.0f;

    // Button background
    auto bgColor = colors.panel;
    if (shouldDrawButtonAsDown)
        bgColor = bgColor.darker(0.15f);
    else if (shouldDrawButtonAsHighlighted)
        bgColor = bgColor.brighter(0.1f);
    
    g.setColour(bgColor);
    g.fillRoundedRectangle(4, (bounds.getHeight() - switchHeight) / 2, switchWidth, switchHeight, 3.0f);

    // Indicator light (small LED-style)
    auto ledX = 8.0f;
    auto ledY = (bounds.getHeight() - 8.0f) / 2;
    auto ledColor = button.getToggleState() ? colors.accent : juce::Colour(0xFF3A4550);

    if (button.getToggleState())
    {
        // Glow effect when on
        g.setColour(ledColor.withAlpha(0.4f));
        g.fillEllipse(ledX - 2, ledY - 2, 12.0f, 12.0f);
    }

    g.setColour(ledColor);
    g.fillEllipse(ledX, ledY, 8.0f, 8.0f);

    // Highlight on LED
    if (button.getToggleState())
    {
        g.setColour(juce::Colour(0xFFFFFFFF).withAlpha(0.4f));
        g.fillEllipse(ledX + 1, ledY + 1, 3.0f, 3.0f);
    }

    // Button border
    g.setColour(colors.text.withAlpha(0.3f));
    g.drawRoundedRectangle(4, (bounds.getHeight() - switchHeight) / 2, switchWidth, switchHeight, 3.0f, 1.0f);

    // Label
    g.setColour(colors.text);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withTypefaceStyle("Bold"));
    g.drawText(button.getButtonText(), switchWidth + 12, 0,
               bounds.getWidth() - switchWidth - 12, bounds.getHeight(),
               juce::Justification::centredLeft);
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
    // The compressor already has its own envelope follower with attack/release
    // We should directly display the gain reduction without additional smoothing
    // This way the meter follows the actual compressor behavior including release time
    currentLevel = targetLevel;
    
    const float frameRate = 60.0f;  // For peak hold decay
    
    // For gain reduction meter: 0 dB = rest position (no compression)
    // Negative values show gain reduction (compression)
    // currentLevel is gain reduction in dB (negative when compressing, 0 when not)
    float displayValue = currentLevel;
    
    // Ensure 0 is treated as 0 (no reduction)
    if (std::abs(displayValue) < 0.001f)
        displayValue = 0.0f;
    
    displayValue = juce::jlimit(-20.0f, 3.0f, displayValue);
    
    // Map to needle position: -20dB to +3dB range
    // -20dB = 0.0 (full left), 0dB = 0.87 (rest position), +3dB = 1.0 (full right)
    // This gives 0dB its proper position on the scale
    float normalizedPos = (displayValue + 20.0f) / 23.0f;  // Normalize to 0-1 range
    float targetNeedle = juce::jlimit(0.0f, 1.0f, normalizedPos);
    
    // Very light smoothing just for visual appeal, but fast enough to follow the compressor
    const float needleSmoothing = 0.35f;  // Faster response to match compressor envelope
    needlePosition += (targetNeedle - needlePosition) * needleSmoothing;
    
    // Peak hold decay
    if (peakHoldTime > 0)
    {
        peakHoldTime -= 1.0f / frameRate;
        if (peakHoldTime <= 0)
            peakLevel = currentLevel;
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
// VU Meter with Label
VUMeterWithLabel::VUMeterWithLabel()
{
    vuMeter = std::make_unique<AnalogVUMeter>();
    addAndMakeVisible(vuMeter.get());
}

void VUMeterWithLabel::setLevel(float newLevel)
{
    if (vuMeter)
        vuMeter->setLevel(newLevel);
}

void VUMeterWithLabel::resized()
{
    auto bounds = getLocalBounds();
    
    // Reserve space for LEVEL label at bottom
    auto labelHeight = juce::jmin(30, bounds.getHeight() / 8);
    auto meterBounds = bounds.removeFromTop(bounds.getHeight() - labelHeight);
    
    if (vuMeter)
        vuMeter->setBounds(meterBounds);
}

void VUMeterWithLabel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    
    // Calculate scale factor based on component size
    float scaleFactor = juce::jmin(bounds.getWidth() / 400.0f, bounds.getHeight() / 280.0f);
    scaleFactor = juce::jmax(0.5f, scaleFactor);
    
    // Draw LEVEL label at bottom
    auto labelHeight = juce::jmin(30.0f, bounds.getHeight() / 8.0f);
    auto labelArea = bounds.removeFromBottom(labelHeight);
    
    g.setColour(juce::Colour(0xFF2A2A2A));
    float fontSize = juce::jmax(12.0f, 16.0f * scaleFactor);
    g.setFont(juce::Font(juce::FontOptions(fontSize)).withTypefaceStyle("Regular"));
    g.drawText("LEVEL", labelArea, juce::Justification::centred);
}

//==============================================================================
// LED Meter
LEDMeter::LEDMeter(Orientation orient) : orientation(orient)
{
}

void LEDMeter::setLevel(float newLevel)
{
    // Clamp to reasonable dB range
    newLevel = juce::jlimit(-60.0f, 6.0f, newLevel);
    
    // Always update and repaint if the level has changed at all
    if (std::abs(newLevel - currentLevel) > 0.01f)
    {
        currentLevel = newLevel;
        repaint();
    }
}

juce::Colour LEDMeter::getLEDColor(int ledIndex, int totalLEDs)
{
    float position = ledIndex / (float)totalLEDs;
    
    if (position < 0.5f)
        return juce::Colour(0xFF00FF00);  // Green
    else if (position < 0.75f)
        return juce::Colour(0xFFFFFF00);  // Yellow
    else if (position < 0.9f)
        return juce::Colour(0xFFFF6600);  // Orange
    else
        return juce::Colour(0xFFFF0000);  // Red
}

void LEDMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    
    // Background
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(bounds, 3.0f);
    
    // Calculate lit LEDs based on level
    // Map -60dB to 0dB range to 0.0 to 1.0
    // -18dB should map to (42/60) = 0.7
    float normalizedLevel = juce::jlimit(0.0f, 1.0f, (currentLevel + 60.0f) / 66.0f); // Extended range to +6dB
    int litLEDs = juce::roundToInt(normalizedLevel * numLEDs);
    
    if (orientation == Vertical)
    {
        float ledHeight = (bounds.getHeight() - (numLEDs + 1) * 2) / numLEDs;
        float ledWidth = bounds.getWidth() - 6;
        
        for (int i = 0; i < numLEDs; ++i)
        {
            float y = bounds.getBottom() - 3 - (i + 1) * (ledHeight + 2);
            
            // LED background
            g.setColour(juce::Colour(0xFF0A0A0A));
            g.fillRoundedRectangle(3, y, ledWidth, ledHeight, 1.0f);
            
            // LED lit state
            if (i < litLEDs)
            {
                auto ledColor = getLEDColor(i, numLEDs);
                
                // Glow effect
                g.setColour(ledColor.withAlpha(0.3f));
                g.fillRoundedRectangle(2, y - 1, ledWidth + 2, ledHeight + 2, 1.0f);
                
                // Main LED
                g.setColour(ledColor);
                g.fillRoundedRectangle(3, y, ledWidth, ledHeight, 1.0f);
                
                // Highlight
                g.setColour(ledColor.brighter(0.5f).withAlpha(0.5f));
                g.fillRoundedRectangle(4, y + 1, ledWidth - 2, ledHeight / 3, 1.0f);
            }
        }
    }
    else // Horizontal
    {
        float ledWidth = (bounds.getWidth() - (numLEDs + 1) * 2) / numLEDs;
        float ledHeight = bounds.getHeight() - 6;
        
        for (int i = 0; i < numLEDs; ++i)
        {
            float x = 3 + i * (ledWidth + 2);
            
            // LED background
            g.setColour(juce::Colour(0xFF0A0A0A));
            g.fillRoundedRectangle(x, 3, ledWidth, ledHeight, 1.0f);
            
            // LED lit state
            if (i < litLEDs)
            {
                auto ledColor = getLEDColor(i, numLEDs);
                
                // Glow effect
                g.setColour(ledColor.withAlpha(0.3f));
                g.fillRoundedRectangle(x - 1, 2, ledWidth + 2, ledHeight + 2, 1.0f);
                
                // Main LED
                g.setColour(ledColor);
                g.fillRoundedRectangle(x, 3, ledWidth, ledHeight, 1.0f);
            }
        }
    }
    
    // Frame
    g.setColour(juce::Colour(0xFF4A4A4A));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

//==============================================================================
// Ratio Button Group
RatioButtonGroup::RatioButtonGroup()
{
    const juce::StringArray ratios = {"4:1", "8:1", "12:1", "20:1", "All"};
    
    for (const auto& ratio : ratios)
    {
        auto button = std::make_unique<juce::TextButton>(ratio);
        button->setClickingTogglesState(true);
        button->setRadioGroupId(1001);
        button->addListener(this);
        addAndMakeVisible(button.get());
        ratioButtons.push_back(std::move(button));
    }
    
    ratioButtons[0]->setToggleState(true, juce::dontSendNotification);
}

RatioButtonGroup::~RatioButtonGroup()
{
    for (auto& button : ratioButtons)
        button->removeListener(this);
}

void RatioButtonGroup::setSelectedRatio(int index)
{
    if (index >= 0 && index < ratioButtons.size())
    {
        ratioButtons[index]->setToggleState(true, juce::dontSendNotification);
        currentRatio = index;
    }
}

void RatioButtonGroup::resized()
{
    auto bounds = getLocalBounds();
    int buttonWidth = bounds.getWidth() / ratioButtons.size();
    
    for (size_t i = 0; i < ratioButtons.size(); ++i)
    {
        ratioButtons[i]->setBounds(i * buttonWidth, 0, buttonWidth - 2, bounds.getHeight());
    }
}

void RatioButtonGroup::buttonClicked(juce::Button* button)
{
    for (size_t i = 0; i < ratioButtons.size(); ++i)
    {
        if (ratioButtons[i].get() == button)
        {
            currentRatio = i;
            listeners.call(&Listener::ratioChanged, currentRatio);
            break;
        }
    }
}
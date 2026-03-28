#include "MultiSynthLookAndFeel.h"
#include <cmath>

//==============================================================================
// Base class implementations

juce::Label* MultiSynthLookAndFeelBase::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setColour(juce::Label::textColourId, colors.text);
    label->setColour(juce::Label::backgroundColourId, juce::Colour(0x40000000));
    label->setColour(juce::Label::outlineColourId, juce::Colour(0x20FFFFFF));
    label->setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    return label;
}

void MultiSynthLookAndFeelBase::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour(juce::Colour(0x50000000));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 3.0f);
}

void MultiSynthLookAndFeelBase::drawComboBox(juce::Graphics& g, int width, int height,
                                              bool /*isButtonDown*/, int, int, int, int,
                                              juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    g.setColour(colors.sectionBackground.darker(0.3f));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(colors.sectionBorder);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Arrow
    auto arrowZone = juce::Rectangle<float>(static_cast<float>(width) - 20.0f, 0.0f, 16.0f, static_cast<float>(height));
    juce::Path arrow;
    arrow.addTriangle(arrowZone.getCentreX() - 4.0f, arrowZone.getCentreY() - 2.0f,
                      arrowZone.getCentreX() + 4.0f, arrowZone.getCentreY() - 2.0f,
                      arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
    g.setColour(colors.accent);
    g.fillPath(arrow);

    juce::ignoreUnused(box);
}

void MultiSynthLookAndFeelBase::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                                  float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                                  juce::Slider::SliderStyle style, juce::Slider& /*slider*/)
{
    if (style != juce::Slider::LinearVertical)
        return; // Only vertical faders used in our UI

    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                          static_cast<float>(width), static_cast<float>(height));

    // Track (recessed slot)
    float trackW = juce::jmax(4.0f, bounds.getWidth() * 0.2f);
    auto trackRect = juce::Rectangle<float>(bounds.getCentreX() - trackW / 2,
                                             bounds.getY() + 4, trackW, bounds.getHeight() - 8);
    g.setColour(colors.shadow);
    g.fillRoundedRectangle(trackRect, 2.0f);
    g.setColour(colors.sectionBorder.withAlpha(0.3f));
    g.drawRoundedRectangle(trackRect, 2.0f, 0.5f);

    // Filled portion (from bottom to current position)
    float thumbY = sliderPos;
    auto fillRect = trackRect.withTop(thumbY);
    g.setColour(colors.sliderTrack.withAlpha(0.5f));
    g.fillRoundedRectangle(fillRect, 2.0f);

    // Thumb (horizontal bar)
    float thumbH = juce::jmax(6.0f, bounds.getWidth() * 0.35f);
    float thumbW = bounds.getWidth() * 0.85f;
    auto thumbRect = juce::Rectangle<float>(bounds.getCentreX() - thumbW / 2,
                                             thumbY - thumbH / 2,
                                             thumbW, thumbH);

    // Thumb shadow
    g.setColour(colors.shadow.withAlpha(0.4f));
    g.fillRoundedRectangle(thumbRect.translated(1.0f, 1.0f), 2.0f);

    // Thumb body
    juce::ColourGradient thumbGrad(colors.knobBody.brighter(0.2f),
                                    thumbRect.getX(), thumbRect.getY(),
                                    colors.knobBody.darker(0.2f),
                                    thumbRect.getX(), thumbRect.getBottom(), false);
    g.setGradientFill(thumbGrad);
    g.fillRoundedRectangle(thumbRect, 2.0f);

    // Thumb center line (indicator)
    g.setColour(colors.knobIndicator.withAlpha(0.7f));
    g.drawHorizontalLine(static_cast<int>(thumbY),
                         thumbRect.getX() + 3, thumbRect.getRight() - 3);

    // Tick marks on panel (left side)
    for (int i = 0; i <= 10; ++i)
    {
        float tickY = bounds.getY() + 4 + (1.0f - i / 10.0f) * (bounds.getHeight() - 8);
        float tickW = (i == 0 || i == 5 || i == 10) ? 6.0f : 3.0f;
        g.setColour(colors.textDim.withAlpha(0.4f));
        g.drawHorizontalLine(static_cast<int>(tickY),
                             bounds.getX(), bounds.getX() + tickW);
    }
}

void MultiSynthLookAndFeelBase::paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                                              const juce::String& title, float scaleFactor) const
{
    g.setColour(colors.sectionBackground);
    g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    g.setColour(colors.sectionBorder.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 6.0f, 1.0f);

    g.setFont(juce::Font(juce::FontOptions(11.0f * scaleFactor).withStyle("Bold")));
    g.setColour(colors.accent);
    g.drawText(title, bounds.getX() + 8, bounds.getY() + 2,
               bounds.getWidth() - 16, static_cast<int>(16 * scaleFactor),
               juce::Justification::centredLeft);
}

void MultiSynthLookAndFeelBase::paintBackground(juce::Graphics& g, int width, int height) const
{
    g.setColour(colors.panelBackground);
    g.fillRect(0, 0, width, height);
}

void MultiSynthLookAndFeelBase::drawIlluminatedToggleButton(
    juce::Graphics& g, juce::ToggleButton& button,
    bool highlighted, bool down,
    const juce::Colour& onTop, const juce::Colour& onBottom, const juce::Colour& onText,
    const juce::Colour& offTop, const juce::Colour& offBottom, const juce::Colour& offText)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    bool isOn = button.getToggleState();

    g.setColour(juce::Colour(0xFF0A0A0A));
    g.fillRoundedRectangle(bounds, 4.0f);

    auto inner = bounds.reduced(2.0f);
    if (isOn)
    {
        juce::ColourGradient glow(onTop, inner.getCentreX(), inner.getY(),
                                  onBottom, inner.getCentreX(), inner.getBottom(), false);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(inner, 3.0f);
        g.setColour(onTop.withAlpha(0.2f));
        g.fillRoundedRectangle(bounds.expanded(1), 5.0f);
        g.setColour(onText);
    }
    else
    {
        juce::ColourGradient dark(offTop, inner.getCentreX(), inner.getY(),
                                  offBottom, inner.getCentreX(), inner.getBottom(), false);
        g.setGradientFill(dark);
        g.fillRoundedRectangle(inner, 3.0f);
        g.setColour(offText);
    }

    if (down)
    {
        g.setColour(juce::Colour(0x20000000));
        g.fillRoundedRectangle(inner, 3.0f);
    }
    else if (highlighted && !isOn)
    {
        g.setColour(juce::Colour(0x10FFFFFF));
        g.fillRoundedRectangle(inner, 3.0f);
    }

    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.drawText(button.getButtonText(), inner, juce::Justification::centred);
}

//==============================================================================
// Chrome knob (Cosmos) — silver metallic, precise, Japanese industrial
void MultiSynthLookAndFeelBase::drawChromeKnob(juce::Graphics& g, float x, float y, float w, float h,
                                                 float sliderPos, float startAngle, float endAngle)
{
    auto radius = juce::jmin(w / 2, h / 2) - 4.0f;
    auto cx = x + w * 0.5f;
    auto cy = y + h * 0.5f;
    auto rx = cx - radius;
    auto ry = cy - radius;
    auto rw = radius * 2.0f;
    auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Drop shadow
    g.setColour(colors.shadow.withAlpha(0.5f));
    g.fillEllipse(rx + 2, ry + 2, rw, rw);

    // Metallic bezel
    juce::ColourGradient bezel(juce::Colour(0xFF8A8A9A), cx - radius, cy,
                               juce::Colour(0xFF3A3A4A), cx + radius, cy, false);
    g.setGradientFill(bezel);
    g.fillEllipse(rx - 2, ry - 2, rw + 4, rw + 4);

    // Chrome body
    juce::ColourGradient body(colors.knobBody.brighter(0.3f), cx, ry,
                              colors.knobBody.darker(0.3f), cx, ry + rw, false);
    g.setGradientFill(body);
    g.fillEllipse(rx, ry, rw, rw);

    // Center cap
    auto capR = radius * 0.35f;
    g.setColour(juce::Colour(0xFF505060));
    g.fillEllipse(cx - capR, cy - capR, capR * 2, capR * 2);
    g.setColour(juce::Colour(0xFF3A3A4A));
    g.drawLine(cx - capR * 0.5f, cy, cx + capR * 0.5f, cy, 0.8f);

    // Value arc behind knob
    juce::Path arcTrack;
    arcTrack.addCentredArc(cx, cy, radius + 5, radius + 5, 0, startAngle, endAngle, true);
    g.setColour(colors.sliderTrack.withAlpha(0.2f));
    g.strokePath(arcTrack, juce::PathStrokeType(3.0f));

    juce::Path arcFill;
    arcFill.addCentredArc(cx, cy, radius + 5, radius + 5, 0, startAngle, angle, true);
    g.setColour(colors.sliderTrack);
    g.strokePath(arcFill, juce::PathStrokeType(3.0f));

    // White indicator line
    juce::Path pointer;
    pointer.addRectangle(-1.5f, -radius + 6, 3.0f, radius * 0.4f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
    g.setColour(colors.knobIndicator);
    g.fillPath(pointer);

    // Tick marks
    for (int i = 0; i < 11; ++i)
    {
        auto tickAngle = startAngle + (i / 10.0f) * (endAngle - startAngle);
        auto tickLen = (i == 0 || i == 10 || i == 5) ? radius * 0.15f : radius * 0.08f;
        juce::Path tick;
        tick.addRectangle(-0.8f, -radius - 8, 1.6f, tickLen);
        tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(cx, cy));
        g.setColour(colors.textDim.withAlpha(0.6f));
        g.fillPath(tick);
    }
}

// Bakelite knob (Oracle) — black body, amber chicken-head pointer, brass screw
void MultiSynthLookAndFeelBase::drawBakeliteKnob(juce::Graphics& g, float x, float y, float w, float h,
                                                    float sliderPos, float startAngle, float endAngle)
{
    auto radius = juce::jmin(w / 2, h / 2) - 4.0f;
    auto cx = x + w * 0.5f;
    auto cy = y + h * 0.5f;
    auto rx = cx - radius;
    auto ry = cy - radius;
    auto rw = radius * 2.0f;
    auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Shadow
    g.setColour(juce::Colour(0x40000000));
    g.fillEllipse(rx + 3, ry + 3, rw, rw);

    // Bakelite body
    juce::ColourGradient body(colors.knobBody.brighter(0.2f), cx - radius, cy - radius,
                              colors.knobBody.darker(0.4f), cx + radius, cy + radius, true);
    g.setGradientFill(body);
    g.fillEllipse(rx, ry, rw, rw);

    // Inner ring
    g.setColour(colors.knobBody.darker(0.6f));
    g.drawEllipse(rx + 4, ry + 4, rw - 8, rw - 8, 1.5f);

    // Warm amber arc (value indicator)
    juce::Path arcFill;
    arcFill.addCentredArc(cx, cy, radius + 5, radius + 5, 0, startAngle, angle, true);
    g.setColour(colors.knobIndicator.withAlpha(0.4f));
    g.strokePath(arcFill, juce::PathStrokeType(3.0f));
    // Glow
    g.setColour(colors.knobIndicator.withAlpha(0.15f));
    g.strokePath(arcFill, juce::PathStrokeType(7.0f));

    // Chicken-head pointer
    juce::Path pointer;
    pointer.startNewSubPath(0, -radius + 8);
    pointer.lineTo(-6, -radius + 24);
    pointer.lineTo(6, -radius + 24);
    pointer.closeSubPath();
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
    g.setColour(colors.knobIndicator);
    g.fillPath(pointer);
    g.setColour(colors.knobIndicator.darker(0.3f));
    g.strokePath(pointer, juce::PathStrokeType(1.0f));

    // Brass center screw
    g.setColour(juce::Colour(0xFFB89040));
    g.fillEllipse(cx - 3, cy - 3, 6, 6);
    g.setColour(juce::Colour(0xFF806020));
    g.drawLine(cx - 2, cy, cx + 2, cy, 0.8f);
}

// Chunky knob (Mono) — black body with colored outer ring
void MultiSynthLookAndFeelBase::drawChunkyKnob(juce::Graphics& g, float x, float y, float w, float h,
                                                  float sliderPos, float startAngle, float endAngle)
{
    auto radius = juce::jmin(w / 2, h / 2) - 4.0f;
    auto cx = x + w * 0.5f;
    auto cy = y + h * 0.5f;
    auto rx = cx - radius;
    auto ry = cy - radius;
    auto rw = radius * 2.0f;
    auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Shadow
    g.setColour(colors.shadow.withAlpha(0.6f));
    g.fillEllipse(rx + 2, ry + 2, rw, rw);

    // Colored outer ring
    g.setColour(colors.accent);
    g.fillEllipse(rx - 2, ry - 2, rw + 4, rw + 4);

    // Black body
    juce::ColourGradient body(colors.knobBody.brighter(0.15f), cx, ry,
                              colors.knobBody.darker(0.2f), cx, ry + rw, false);
    g.setGradientFill(body);
    g.fillEllipse(rx, ry, rw, rw);

    // Value arc on panel surface
    juce::Path arcTrack;
    arcTrack.addCentredArc(cx, cy, radius + 6, radius + 6, 0, startAngle, endAngle, true);
    g.setColour(colors.sliderTrack.withAlpha(0.15f));
    g.strokePath(arcTrack, juce::PathStrokeType(3.0f));

    juce::Path arcFill;
    arcFill.addCentredArc(cx, cy, radius + 6, radius + 6, 0, startAngle, angle, true);
    g.setColour(colors.accent);
    g.strokePath(arcFill, juce::PathStrokeType(3.0f));

    // Bold white wedge pointer
    juce::Path pointer;
    pointer.addRectangle(-2.5f, -radius + 4, 5.0f, radius * 0.45f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
    g.setColour(colors.knobIndicator);
    g.fillPath(pointer);

    // Flat center cap
    auto capR = radius * 0.25f;
    g.setColour(colors.knobBody.darker(0.3f));
    g.fillEllipse(cx - capR, cy - capR, capR * 2, capR * 2);

    // Panel tick marks (printed on metal surface, not on knob)
    for (int i = 0; i < 11; ++i)
    {
        auto tickAngle = startAngle + (i / 10.0f) * (endAngle - startAngle);
        auto tickLen = (i == 0 || i == 10 || i == 5) ? radius * 0.18f : radius * 0.1f;
        juce::Path tick;
        tick.addRectangle(-1.0f, -radius - 10, 2.0f, tickLen);
        tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(cx, cy));
        g.setColour(colors.text.withAlpha(0.5f));
        g.fillPath(tick);
    }
}

// Davies knob (Modular) — brown Bakelite with cream triangular pointer, knurled ring
void MultiSynthLookAndFeelBase::drawDaviesKnob(juce::Graphics& g, float x, float y, float w, float h,
                                                  float sliderPos, float startAngle, float endAngle)
{
    auto radius = juce::jmin(w / 2, h / 2) - 4.0f;
    auto cx = x + w * 0.5f;
    auto cy = y + h * 0.5f;
    auto rx = cx - radius;
    auto ry = cy - radius;
    auto rw = radius * 2.0f;
    auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Shadow
    g.setColour(juce::Colour(0x60000000));
    g.fillEllipse(rx + 3, ry + 3, rw, rw);

    // Knurled ring (simulated with darker outer ring + radial lines)
    g.setColour(colors.knobBody.darker(0.5f));
    g.fillEllipse(rx - 3, ry - 3, rw + 6, rw + 6);
    for (int i = 0; i < 32; ++i)
    {
        float a = static_cast<float>(i) / 32.0f * juce::MathConstants<float>::twoPi;
        float ir = radius + 1;
        float or_ = radius + 3;
        g.setColour(colors.knobBody.brighter(0.1f).withAlpha(0.3f));
        g.drawLine(cx + std::cos(a) * ir, cy + std::sin(a) * ir,
                   cx + std::cos(a) * or_, cy + std::sin(a) * or_, 0.8f);
    }

    // Bakelite body
    juce::ColourGradient body(colors.knobBody.brighter(0.15f), cx - radius, cy - radius,
                              colors.knobBody.darker(0.3f), cx + radius, cy + radius, true);
    g.setGradientFill(body);
    g.fillEllipse(rx, ry, rw, rw);

    // Cream triangular pointer (chicken-head, larger)
    juce::Path pointer;
    pointer.startNewSubPath(0, -radius + 6);
    pointer.lineTo(-7, -radius + 22);
    pointer.lineTo(7, -radius + 22);
    pointer.closeSubPath();
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
    g.setColour(colors.knobIndicator);
    g.fillPath(pointer);

    // Center screw
    g.setColour(juce::Colour(0xFF2A2018));
    g.fillEllipse(cx - 3, cy - 3, 6, 6);
    g.setColour(juce::Colour(0xFF1A1008));
    g.drawLine(cx - 2, cy - 2, cx + 2, cy + 2, 0.8f);
    g.drawLine(cx + 2, cy - 2, cx - 2, cy + 2, 0.8f);

    // Cream position marks on black panel (hand-drawn style)
    for (int i = 0; i < 11; ++i)
    {
        auto tickAngle = startAngle + (i / 10.0f) * (endAngle - startAngle);
        auto tickLen = (i == 0 || i == 10 || i == 5) ? radius * 0.18f : radius * 0.1f;
        juce::Path tick;
        float lineW = (i == 0 || i == 10 || i == 5) ? 1.5f : 1.0f;
        tick.addRectangle(-lineW * 0.5f, -radius - 10, lineW, tickLen);
        tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(cx, cy));
        g.setColour(colors.sectionBorder.withAlpha(0.7f)); // cream
        g.fillPath(tick);
    }
}

//==============================================================================
// COSMOS — Japanese polysynth, 1981
CosmosLookAndFeel::CosmosLookAndFeel()
{
    colors.panelBackground   = juce::Colour(0xFF1A1C2E);
    colors.sectionBackground = juce::Colour(0xFF22243A);
    colors.sectionBorder     = juce::Colour(0xFF3A3E5C);
    colors.knobBody          = juce::Colour(0xFFC0C0CC);
    colors.knobIndicator     = juce::Colour(0xFFFFFFFF);
    colors.accent            = juce::Colour(0xFF6070DD);
    colors.text              = juce::Colour(0xFFD8D8E8);
    colors.textDim           = juce::Colour(0xFF707090);
    colors.sliderTrack       = juce::Colour(0xFF4050AA);
    colors.shadow            = juce::Colour(0xFF0A0A14);

    setColour(juce::ResizableWindow::backgroundColourId, colors.panelBackground);
    setColour(juce::Label::textColourId, colors.text);
    setColour(juce::ComboBox::textColourId, colors.text);
    setColour(juce::PopupMenu::backgroundColourId, colors.sectionBackground);
    setColour(juce::PopupMenu::textColourId, colors.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colors.accent);
}

void CosmosLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float start, float end, juce::Slider&)
{
    drawChromeKnob(g, static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(w), static_cast<float>(h), pos, start, end);
}

void CosmosLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hl, bool dn)
{
    drawIlluminatedToggleButton(g, b, hl, dn,
        juce::Colour(0xFF6070DD), juce::Colour(0xFF4050AA), juce::Colour(0xFFFFFFFF),
        juce::Colour(0xFF2A2C3E), juce::Colour(0xFF1A1C2E), colors.textDim);
}

void CosmosLookAndFeel::paintBackground(juce::Graphics& g, int width, int height) const
{
    // Navy gradient
    juce::ColourGradient bg(colors.panelBackground, 0, 0,
                            colors.panelBackground.darker(0.3f), 0, static_cast<float>(height), false);
    g.setGradientFill(bg);
    g.fillAll();

    // Subtle horizontal brushed lines
    juce::Random rng(42);
    for (int i = 0; i < 150; ++i)
    {
        int ly = rng.nextInt(height);
        g.setColour(juce::Colour(0xFF3040AA).withAlpha(0.025f));
        g.drawHorizontalLine(ly, 0.0f, static_cast<float>(width));
    }
}

void CosmosLookAndFeel::paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                                      const juce::String& title, float sf) const
{
    // Dark navy fill with inner highlight
    g.setColour(colors.sectionBackground);
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

    // Border
    g.setColour(colors.sectionBorder.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

    // Inner top highlight
    g.setColour(juce::Colour(0x08FFFFFF));
    g.drawHorizontalLine(bounds.getY() + 1, static_cast<float>(bounds.getX() + 4),
                         static_cast<float>(bounds.getRight() - 4));

    // Title — clean sans-serif, accent blue
    g.setFont(juce::Font(juce::FontOptions(10.0f * sf).withStyle("Bold")));
    g.setColour(colors.accent);
    g.drawText(title, bounds.getX() + 8, bounds.getY() + 2,
               bounds.getWidth() - 16, static_cast<int>(16 * sf),
               juce::Justification::centredLeft);
}

//==============================================================================
// ORACLE — American polysynth, 1978
OracleLookAndFeel::OracleLookAndFeel()
{
    colors.panelBackground   = juce::Colour(0xFF1C1610);
    colors.sectionBackground = juce::Colour(0xFF2A2018);
    colors.sectionBorder     = juce::Colour(0xFF4A3828);
    colors.knobBody          = juce::Colour(0xFF1A1A1A);
    colors.knobIndicator     = juce::Colour(0xFFE8A030);
    colors.accent            = juce::Colour(0xFFD08020);
    colors.text              = juce::Colour(0xFFE8D8C0);
    colors.textDim           = juce::Colour(0xFF8A7060);
    colors.sliderTrack       = juce::Colour(0xFFCC7020);
    colors.shadow            = juce::Colour(0xFF0A0806);

    setColour(juce::ResizableWindow::backgroundColourId, colors.panelBackground);
    setColour(juce::Label::textColourId, colors.text);
    setColour(juce::ComboBox::textColourId, colors.text);
    setColour(juce::PopupMenu::backgroundColourId, colors.sectionBackground);
    setColour(juce::PopupMenu::textColourId, colors.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colors.accent);
}

void OracleLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float start, float end, juce::Slider&)
{
    drawBakeliteKnob(g, static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(w), static_cast<float>(h), pos, start, end);
}

void OracleLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hl, bool dn)
{
    drawIlluminatedToggleButton(g, b, hl, dn,
        juce::Colour(0xFFD08020), juce::Colour(0xFFA06010), juce::Colour(0xFF1A1008),
        juce::Colour(0xFF3A3020), juce::Colour(0xFF2A2018), colors.textDim);
}

void OracleLookAndFeel::paintBackground(juce::Graphics& g, int width, int height) const
{
    g.setColour(colors.panelBackground);
    g.fillAll();

    // Wood grain texture — horizontal lines in warm browns
    juce::Random rng(7);
    for (int i = 0; i < 80; ++i)
    {
        int ly = rng.nextInt(height);
        float thickness = 1.0f + rng.nextFloat() * 2.0f;
        float alpha = 0.04f + rng.nextFloat() * 0.08f;
        g.setColour(juce::Colour(0xFF3A2E20).withAlpha(alpha));
        g.fillRect(0.0f, static_cast<float>(ly), static_cast<float>(width), thickness);
    }

    // Edge shadows (wood panel depth)
    juce::ColourGradient leftShadow(juce::Colour(0x30000000), 0, 0,
                                     juce::Colour(0x00000000), 8.0f, 0, false);
    g.setGradientFill(leftShadow);
    g.fillRect(0, 0, 8, height);

    juce::ColourGradient rightShadow(juce::Colour(0x00000000), static_cast<float>(width - 8), 0,
                                      juce::Colour(0x30000000), static_cast<float>(width), 0, false);
    g.setGradientFill(rightShadow);
    g.fillRect(width - 8, 0, 8, height);
}

void OracleLookAndFeel::paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                                      const juce::String& title, float sf) const
{
    g.setColour(colors.sectionBackground);
    g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    g.setColour(colors.sectionBorder.withAlpha(0.6f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 6.0f, 1.5f);

    // Gold highlight line at top
    g.setColour(juce::Colour(0x15AA8040));
    g.drawHorizontalLine(bounds.getY() + 1, static_cast<float>(bounds.getX() + 6),
                         static_cast<float>(bounds.getRight() - 6));

    // Serif-style title
    g.setFont(juce::Font(juce::FontOptions(11.0f * sf)));
    g.setColour(colors.text);
    g.drawText(title, bounds.getX() + 8, bounds.getY() + 2,
               bounds.getWidth() - 16, static_cast<int>(16 * sf),
               juce::Justification::centredLeft);
}

//==============================================================================
// MONO — Japanese monosynth, 1979
MonoLookAndFeel::MonoLookAndFeel()
{
    colors.panelBackground   = juce::Colour(0xFF404448);
    colors.sectionBackground = juce::Colour(0xFF4A4E54);
    colors.sectionBorder     = juce::Colour(0xFF606468);
    colors.knobBody          = juce::Colour(0xFF2A2A2A);
    colors.knobIndicator     = juce::Colour(0xFFFF4040);
    colors.accent            = juce::Colour(0xFFE04030);
    colors.text              = juce::Colour(0xFFF0F0F0);
    colors.textDim           = juce::Colour(0xFF909498);
    colors.sliderTrack       = juce::Colour(0xFFE04030);
    colors.shadow            = juce::Colour(0xFF202224);

    setColour(juce::ResizableWindow::backgroundColourId, colors.panelBackground);
    setColour(juce::Label::textColourId, colors.text);
    setColour(juce::ComboBox::textColourId, colors.text);
    setColour(juce::PopupMenu::backgroundColourId, colors.sectionBackground);
    setColour(juce::PopupMenu::textColourId, colors.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colors.accent);
}

void MonoLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                        float pos, float start, float end, juce::Slider&)
{
    drawChunkyKnob(g, static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(w), static_cast<float>(h), pos, start, end);
}

void MonoLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hl, bool dn)
{
    drawIlluminatedToggleButton(g, b, hl, dn,
        juce::Colour(0xFFE04030), juce::Colour(0xFFB03020), juce::Colour(0xFFFFFFFF),
        juce::Colour(0xFF505458), juce::Colour(0xFF404448), colors.textDim);
}

void MonoLookAndFeel::paintBackground(juce::Graphics& g, int width, int height) const
{
    g.setColour(colors.panelBackground);
    g.fillAll();

    // Brushed aluminum texture
    juce::Random rng(13);
    for (int i = 0; i < 200; ++i)
    {
        int ly = rng.nextInt(height);
        g.setColour(rng.nextBool() ? juce::Colour(0x08FFFFFF) : juce::Colour(0x08000000));
        g.drawHorizontalLine(ly, 0.0f, static_cast<float>(width));
    }

    // Metal panel bevel
    g.setColour(juce::Colour(0x15FFFFFF));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(width));
    g.drawVerticalLine(0, 0.0f, static_cast<float>(height));
    g.setColour(juce::Colour(0x15000000));
    g.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
    g.drawVerticalLine(width - 1, 0.0f, static_cast<float>(height));
}

void MonoLookAndFeel::paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                                    const juce::String& title, float sf) const
{
    g.setColour(colors.sectionBackground);
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    // Beveled border
    g.setColour(juce::Colour(0x10FFFFFF));
    g.drawHorizontalLine(bounds.getY(), static_cast<float>(bounds.getX() + 2),
                         static_cast<float>(bounds.getRight() - 2));
    g.drawVerticalLine(bounds.getX(), static_cast<float>(bounds.getY() + 2),
                       static_cast<float>(bounds.getBottom() - 2));
    g.setColour(juce::Colour(0x15000000));
    g.drawHorizontalLine(bounds.getBottom() - 1, static_cast<float>(bounds.getX() + 2),
                         static_cast<float>(bounds.getRight() - 2));
    g.drawVerticalLine(bounds.getRight() - 1, static_cast<float>(bounds.getY() + 2),
                       static_cast<float>(bounds.getBottom() - 2));

    // Colored section stripe at top
    g.setColour(colors.accent);
    g.fillRect(bounds.getX() + 2, bounds.getY(), bounds.getWidth() - 4, static_cast<int>(3 * sf));

    // Bold centered title
    g.setFont(juce::Font(juce::FontOptions(10.0f * sf).withStyle("Bold")));
    g.setColour(colors.text);
    g.drawText(title.toUpperCase(), bounds.getX() + 4, bounds.getY() + static_cast<int>(3 * sf),
               bounds.getWidth() - 8, static_cast<int>(16 * sf),
               juce::Justification::centred);
}

//==============================================================================
// MODULAR — American semi-modular, 1971
ModularLookAndFeel::ModularLookAndFeel()
{
    colors.panelBackground   = juce::Colour(0xFF0E0E0E);
    colors.sectionBackground = juce::Colour(0xFF161616);
    colors.sectionBorder     = juce::Colour(0xFFD0C8B8);
    colors.knobBody          = juce::Colour(0xFF3A2820);
    colors.knobIndicator     = juce::Colour(0xFFE8E0D0);
    colors.accent            = juce::Colour(0xFFCC4420);
    colors.text              = juce::Colour(0xFFD8D0C0);
    colors.textDim           = juce::Colour(0xFF787068);
    colors.sliderTrack       = juce::Colour(0xFFCC4420);
    colors.shadow            = juce::Colour(0xFF000000);

    setColour(juce::ResizableWindow::backgroundColourId, colors.panelBackground);
    setColour(juce::Label::textColourId, colors.text);
    setColour(juce::ComboBox::textColourId, colors.text);
    setColour(juce::PopupMenu::backgroundColourId, colors.sectionBackground);
    setColour(juce::PopupMenu::textColourId, colors.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colors.accent);
}

void ModularLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                            float pos, float start, float end, juce::Slider&)
{
    drawDaviesKnob(g, static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(w), static_cast<float>(h), pos, start, end);
}

void ModularLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hl, bool dn)
{
    drawIlluminatedToggleButton(g, b, hl, dn,
        juce::Colour(0xFFCC4420), juce::Colour(0xFF993310), juce::Colour(0xFFE8E0D0),
        juce::Colour(0xFF282420), juce::Colour(0xFF161616), colors.textDim);
}

void ModularLookAndFeel::paintBackground(juce::Graphics& g, int width, int height) const
{
    g.setColour(colors.panelBackground);
    g.fillAll();

    // Subtle noise grain
    juce::Random rng(31);
    for (int i = 0; i < 400; ++i)
    {
        int px = rng.nextInt(width);
        int py = rng.nextInt(height);
        g.setColour(juce::Colour(0x08FFFFFF));
        g.fillRect(px, py, 1, 1);
    }
}

void ModularLookAndFeel::paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                                        const juce::String& title, float sf) const
{
    g.setColour(colors.sectionBackground);
    g.fillRoundedRectangle(bounds.toFloat(), 2.0f);

    // Cream silkscreen border
    g.setColour(colors.sectionBorder.withAlpha(0.4f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.0f, 1.0f);

    // Cream centered title
    g.setFont(juce::Font(juce::FontOptions(10.0f * sf)));
    g.setColour(colors.text);
    g.drawText(title.toUpperCase(), bounds.getX() + 4, bounds.getY() + 2,
               bounds.getWidth() - 8, static_cast<int>(16 * sf),
               juce::Justification::centred);
}

void ModularLookAndFeel::paintSpecialElements(juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    // Decorative patch point circles at section edges
    auto drawPatchPoint = [&](float px, float py)
    {
        g.setColour(colors.sectionBorder.withAlpha(0.5f));
        g.drawEllipse(px - 5, py - 5, 10, 10, 1.5f);
        g.setColour(colors.panelBackground);
        g.fillEllipse(px - 3, py - 3, 6, 6);
        g.setColour(colors.sectionBorder.withAlpha(0.3f));
        g.fillEllipse(px - 1.5f, py - 1.5f, 3, 3);
    };

    // Place a few patch points along the right edges of the main sections
    float sectionH = static_cast<float>(bounds.getHeight());
    float rightX = static_cast<float>(bounds.getRight()) - 4;
    drawPatchPoint(rightX, sectionH * 0.15f);
    drawPatchPoint(rightX, sectionH * 0.35f);
    drawPatchPoint(rightX, sectionH * 0.55f);

    juce::ignoreUnused(bounds);
}

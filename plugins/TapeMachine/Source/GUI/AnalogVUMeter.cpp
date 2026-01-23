#include "AnalogVUMeter.h"

// Premium color constants
namespace VUColors
{
    constexpr uint32_t frameOuter    = 0xffb8b0a0;     // Light gray/silver frame
    constexpr uint32_t frameInner    = 0xff3a3a3a;     // Dark inner frame
    constexpr uint32_t faceBase      = 0xfff5f0e6;     // Warm cream faceplate
    constexpr uint32_t faceAged      = 0xfff0e8d8;     // Slightly yellowed cream
    constexpr uint32_t needleRed     = 0xffcc3333;     // Classic red needle
    constexpr uint32_t needleShadow  = 0x40000000;     // Needle shadow
    constexpr uint32_t scaleBlack    = 0xff2a2a2a;     // Scale markings
    constexpr uint32_t scaleRed      = 0xffd42c2c;     // Red zone markings
    constexpr uint32_t pivotBlack    = 0xff000000;     // Needle pivot
    constexpr uint32_t screwChrome   = 0xffb0a898;     // Screw heads
    constexpr uint32_t screwSlot     = 0xff1a1a18;     // Screw slot
}

AnalogVUMeter::AnalogVUMeter()
{
    startTimerHz(60);
}

AnalogVUMeter::~AnalogVUMeter()
{
    stopTimer();
}

void AnalogVUMeter::setStereoMode(bool isStereo)
{
    if (stereoMode != isStereo)
    {
        stereoMode = isStereo;
        cacheNeedsUpdate = true;
        repaint();
    }
}

void AnalogVUMeter::setLevels(float leftLevel, float rightLevel)
{
    // Convert linear to dB with VU calibration offset
    // Standard: 0 VU = +4 dBu = -18 dBFS (IEC 60268-17)
    // This makes the meter read 0 VU at operating level (-18 dBFS RMS)
    constexpr float calibrationOffsetDB = 18.0f;

    float dbL = 20.0f * std::log10(std::max(0.001f, leftLevel)) + calibrationOffsetDB;
    float dbR = 20.0f * std::log10(std::max(0.001f, rightLevel)) + calibrationOffsetDB;

    targetLevelL = dbL;
    targetLevelR = dbR;

    // Update peaks
    if (dbL > peakLevelL)
    {
        peakLevelL = dbL;
        peakHoldTimeL = 2.0f;
    }
    if (dbR > peakLevelR)
    {
        peakLevelR = dbR;
        peakHoldTimeR = 2.0f;
    }
}

void AnalogVUMeter::resized()
{
    cacheNeedsUpdate = true;
}

void AnalogVUMeter::timerCallback()
{
    const float dt = 1.0f / kRefreshRateHz;
    const float vuCoeff = 1.0f - std::exp(-1000.0f * dt / kVUTimeConstantMs);

    // Process left channel with spring physics
    {
        float displayL = juce::jlimit(-20.0f, 3.0f, targetLevelL);
        float targetNeedleL = (displayL + 20.0f) / 23.0f;

        float displacement = targetNeedleL - needlePositionL;
        float springForce = displacement * kOvershootStiffness;
        float dampingForce = -needleVelocityL * kOvershootDamping * 2.0f * std::sqrt(kOvershootStiffness);

        float acceleration = springForce + dampingForce;
        needleVelocityL += acceleration * dt;
        needlePositionL += needleVelocityL * dt;
        needlePositionL += vuCoeff * (targetNeedleL - needlePositionL) * 0.3f;
        needlePositionL = juce::jlimit(0.0f, 1.0f, needlePositionL);

        displacement = targetNeedleL - needlePositionL;
        if (std::abs(needleVelocityL) < 0.001f && std::abs(displacement) < 0.001f)
            needleVelocityL = 0.0f;
    }

    // Process right channel
    {
        float displayR = juce::jlimit(-20.0f, 3.0f, targetLevelR);
        float targetNeedleR = (displayR + 20.0f) / 23.0f;

        float displacement = targetNeedleR - needlePositionR;
        float springForce = displacement * kOvershootStiffness;
        float dampingForce = -needleVelocityR * kOvershootDamping * 2.0f * std::sqrt(kOvershootStiffness);

        float acceleration = springForce + dampingForce;
        needleVelocityR += acceleration * dt;
        needlePositionR += needleVelocityR * dt;
        needlePositionR += vuCoeff * (targetNeedleR - needlePositionR) * 0.3f;
        needlePositionR = juce::jlimit(0.0f, 1.0f, needlePositionR);

        displacement = targetNeedleR - needlePositionR;
        if (std::abs(needleVelocityR) < 0.001f && std::abs(displacement) < 0.001f)
            needleVelocityR = 0.0f;
    }

    // Peak hold decay
    if (peakHoldTimeL > 0)
    {
        peakHoldTimeL -= dt;
        if (peakHoldTimeL <= 0)
            peakLevelL = targetLevelL;
    }
    if (peakHoldTimeR > 0)
    {
        peakHoldTimeR -= dt;
        if (peakHoldTimeR <= 0)
            peakLevelR = targetLevelR;
    }

    repaint();
}

void AnalogVUMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (stereoMode)
    {
        // Stereo: two meters side by side
        float gap = 8.0f;
        float meterWidth = (bounds.getWidth() - gap) / 2.0f;

        auto leftBounds = bounds.withWidth(meterWidth);
        paintSingleMeter(g, leftBounds, needlePositionL, peakLevelL, "L");

        auto rightBounds = bounds.withX(bounds.getX() + meterWidth + gap).withWidth(meterWidth);
        paintSingleMeter(g, rightBounds, needlePositionR, peakLevelR, "R");
    }
    else
    {
        // Mono: single centered meter (same width as stereo individual)
        float gap = 8.0f;
        float meterWidth = (bounds.getWidth() - gap) / 2.0f;
        auto monoBounds = bounds.withSizeKeepingCentre(meterWidth, bounds.getHeight());

        float avgNeedlePos = juce::jmax(needlePositionL, needlePositionR);
        float avgPeakLevel = juce::jmax(peakLevelL, peakLevelR);
        paintSingleMeter(g, monoBounds, avgNeedlePos, avgPeakLevel, "VU");
    }
}

void AnalogVUMeter::paintSingleMeter(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                                      float needlePos, float peakLevel, const juce::String& label)
{
    // Calculate scale factor
    float scale = juce::jmin(bounds.getWidth() / 200.0f, bounds.getHeight() / 140.0f);
    scale = juce::jmax(0.5f, scale);

    // Draw frame and face
    drawMeterFrame(g, bounds, scale);

    auto faceBounds = bounds.reduced(5.0f * scale);
    drawMeterFace(g, faceBounds, scale);

    // Set up meter geometry
    auto centreX = faceBounds.getCentreX();
    auto pivotY = faceBounds.getBottom() - (4 * scale);
    auto maxHeightForText = faceBounds.getHeight() * 0.86f;
    auto maxWidthRadius = faceBounds.getWidth() * 0.48f;
    auto needleLength = juce::jmin(maxWidthRadius, maxHeightForText);

    // Clip to face area
    g.saveState();
    g.reduceClipRegion(faceBounds.toNearestInt());

    // Draw scale markings
    drawScaleMarkings(g, centreX, pivotY, needleLength, scale);

    // Draw label (VU, L, or R)
    g.setColour(juce::Colour(VUColors::scaleBlack));
    float vuFontSize = juce::jmax(10.0f, 14.0f * scale);
    g.setFont(juce::Font(vuFontSize).withTypefaceStyle("Bold"));
    float vuY = pivotY - (needleLength * 0.42f);
    g.drawText(label, centreX - 15 * scale, vuY, 30 * scale, 16 * scale, juce::Justification::centred);

    // Draw needle with shadow
    float needleAngle = kScaleStartAngle + needlePos * (kScaleEndAngle - kScaleStartAngle);
    drawNeedleWithShadow(g, centreX, pivotY, needleLength, needleAngle, scale);

    g.restoreState();

    // Draw decorative screws on the frame
    drawDecoScrews(g, bounds, scale);

    // Draw glass reflection (on top of everything)
    drawGlassReflection(g, faceBounds, scale);
}

void AnalogVUMeter::drawMeterFrame(juce::Graphics& g, const juce::Rectangle<float>& bounds, float scale)
{
    // Outer silver/chrome frame
    juce::ColourGradient frameGrad(
        juce::Colour(VUColors::frameOuter).brighter(0.2f), bounds.getX(), bounds.getY(),
        juce::Colour(VUColors::frameOuter).darker(0.1f), bounds.getRight(), bounds.getBottom(),
        false);
    g.setGradientFill(frameGrad);
    g.fillRoundedRectangle(bounds, 4.0f * scale);

    // Frame bevel highlight (top-left)
    g.setColour(juce::Colour(0x30ffffff));
    g.drawRoundedRectangle(bounds.reduced(1), 4.0f * scale, 1.5f * scale);

    // Inner dark frame
    auto innerFrame = bounds.reduced(3.0f * scale);
    g.setColour(juce::Colour(VUColors::frameInner));
    g.fillRoundedRectangle(innerFrame, 2.5f * scale);

    // Inner frame shadow
    g.setColour(juce::Colour(0x40000000));
    g.drawRoundedRectangle(innerFrame.reduced(1), 2.5f * scale, 1.0f);
}

void AnalogVUMeter::drawMeterFace(juce::Graphics& g, const juce::Rectangle<float>& bounds, float scale)
{
    // Main cream faceplate with subtle radial gradient for depth
    juce::ColourGradient faceGrad(
        juce::Colour(VUColors::faceBase), bounds.getCentreX(), bounds.getCentreY() * 0.8f,
        juce::Colour(VUColors::faceAged), bounds.getCentreX(), bounds.getBottom(),
        true);
    g.setGradientFill(faceGrad);
    g.fillRoundedRectangle(bounds, 2.0f * scale);

    // Subtle texture effect (very light noise pattern)
    juce::Random rng(1234);
    g.setColour(juce::Colour(0x05000000));
    for (int i = 0; i < 50; ++i)
    {
        float x = bounds.getX() + rng.nextFloat() * bounds.getWidth();
        float y = bounds.getY() + rng.nextFloat() * bounds.getHeight();
        g.fillEllipse(x, y, 1.5f * scale, 1.5f * scale);
    }

    // Subtle vignette (darker at edges)
    juce::ColourGradient vignetteGrad(
        juce::Colour(0x00000000), bounds.getCentreX(), bounds.getCentreY(),
        juce::Colour(0x15000000), bounds.getX(), bounds.getY(),
        true);
    g.setGradientFill(vignetteGrad);
    g.fillRoundedRectangle(bounds, 2.0f * scale);
}

void AnalogVUMeter::drawScaleMarkings(juce::Graphics& g, float centreX, float pivotY,
                                       float needleLength, float scale)
{
    // Font for scale numbers
    float baseFontSize = juce::jmax(8.0f, 11.0f * scale);
    g.setFont(juce::Font(baseFontSize));

    // Scale values and their positions
    const float dbValues[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    const int numDbValues = 11;

    for (int i = 0; i < numDbValues; ++i)
    {
        float db = dbValues[i];
        float normalizedPos = (db + 20.0f) / 23.0f;
        float angle = kScaleStartAngle + normalizedPos * (kScaleEndAngle - kScaleStartAngle);

        bool isMajor = (db == -20 || db == -10 || db == -7 || db == -5 || db == -3 ||
                        db == -2 || db == -1 || db == 0 || db == 1 || db == 3);
        bool showText = (db == -20 || db == -10 || db == -5 || db == 0 || db == 3);
        bool isRedZone = (db >= 0);

        // Tick marks
        float tickLength = isMajor ? (8.0f * scale) : (5.0f * scale);
        float tickRadius = needleLength * 0.94f;
        float x1 = centreX + tickRadius * std::cos(angle);
        float y1 = pivotY + tickRadius * std::sin(angle);
        float x2 = centreX + (tickRadius + tickLength) * std::cos(angle);
        float y2 = pivotY + (tickRadius + tickLength) * std::sin(angle);

        g.setColour(juce::Colour(isRedZone ? VUColors::scaleRed : VUColors::scaleBlack));
        g.drawLine(x1, y1, x2, y2, isMajor ? (1.8f * scale) : (1.0f * scale));

        // Text labels
        if (showText)
        {
            float textRadius = needleLength * 0.70f;
            float textX = centreX + textRadius * std::cos(angle);
            float textY = pivotY + textRadius * std::sin(angle);

            float textBoxWidth = 26 * scale;
            float textBoxHeight = 14 * scale;

            juce::String dbText;
            if (db == 0)
                dbText = "0";
            else if (db > 0)
                dbText = "+" + juce::String((int)db);
            else
                dbText = juce::String((int)db);

            g.setColour(juce::Colour(isRedZone ? VUColors::scaleRed : VUColors::scaleBlack));
            g.drawText(dbText, textX - textBoxWidth/2, textY - textBoxHeight/2,
                       textBoxWidth, textBoxHeight, juce::Justification::centred);
        }
    }

    // Draw the red zone arc (0 to +3)
    float arcStartNorm = 20.0f / 23.0f;  // 0 VU
    float arcEndNorm = 1.0f;              // +3 VU
    float arcStart = kScaleStartAngle + arcStartNorm * (kScaleEndAngle - kScaleStartAngle);
    float arcEnd = kScaleStartAngle + arcEndNorm * (kScaleEndAngle - kScaleStartAngle);
    float arcRadius = needleLength * 0.86f;

    juce::Path redArc;
    redArc.addArc(centreX - arcRadius, pivotY - arcRadius,
                  arcRadius * 2, arcRadius * 2,
                  arcStart, arcEnd, true);
    g.setColour(juce::Colour(VUColors::scaleRed).withAlpha(0.6f));
    g.strokePath(redArc, juce::PathStrokeType(3.0f * scale));
}

void AnalogVUMeter::drawNeedleWithShadow(juce::Graphics& g, float centreX, float pivotY,
                                          float needleLength, float needleAngle, float scale)
{
    // Needle shadow (offset and blurred)
    {
        juce::Path shadowPath;
        shadowPath.startNewSubPath(centreX + 2, pivotY + 2);
        shadowPath.lineTo(centreX + 2 + needleLength * 0.95f * std::cos(needleAngle),
                          pivotY + 2 + needleLength * 0.95f * std::sin(needleAngle));
        g.setColour(juce::Colour(VUColors::needleShadow));
        g.strokePath(shadowPath, juce::PathStrokeType(3.0f * scale));
    }

    // Main needle body (tapered)
    {
        juce::Path needle;
        float tipX = centreX + needleLength * 0.95f * std::cos(needleAngle);
        float tipY = pivotY + needleLength * 0.95f * std::sin(needleAngle);

        // Create tapered needle shape
        float baseWidth = 3.5f * scale;
        float perpAngle = needleAngle + juce::MathConstants<float>::halfPi;

        float baseX1 = centreX + baseWidth * 0.5f * std::cos(perpAngle);
        float baseY1 = pivotY + baseWidth * 0.5f * std::sin(perpAngle);
        float baseX2 = centreX - baseWidth * 0.5f * std::cos(perpAngle);
        float baseY2 = pivotY - baseWidth * 0.5f * std::sin(perpAngle);

        needle.startNewSubPath(baseX1, baseY1);
        needle.lineTo(tipX, tipY);
        needle.lineTo(baseX2, baseY2);
        needle.closeSubPath();

        g.setColour(juce::Colour(VUColors::needleRed));
        g.fillPath(needle);

        // Needle highlight (top edge)
        g.setColour(juce::Colour(0x40ffffff));
        g.drawLine(baseX1, baseY1, tipX, tipY, 0.5f * scale);
    }

    // Needle pivot (center cap)
    {
        float pivotRadius = 4.5f * scale;

        // Pivot shadow
        g.setColour(juce::Colour(0x40000000));
        g.fillEllipse(centreX - pivotRadius + 1, pivotY - pivotRadius + 1,
                      pivotRadius * 2, pivotRadius * 2);

        // Pivot body
        juce::ColourGradient pivotGrad(
            juce::Colour(0xff2a2a2a), centreX - pivotRadius * 0.5f, pivotY - pivotRadius * 0.5f,
            juce::Colour(VUColors::pivotBlack), centreX + pivotRadius * 0.5f, pivotY + pivotRadius * 0.5f,
            false);
        g.setGradientFill(pivotGrad);
        g.fillEllipse(centreX - pivotRadius, pivotY - pivotRadius,
                      pivotRadius * 2, pivotRadius * 2);

        // Pivot highlight
        g.setColour(juce::Colour(0x30ffffff));
        g.fillEllipse(centreX - pivotRadius * 0.4f, pivotY - pivotRadius * 0.5f,
                      pivotRadius * 0.6f, pivotRadius * 0.4f);
    }
}

void AnalogVUMeter::drawGlassReflection(juce::Graphics& g, const juce::Rectangle<float>& bounds, float scale)
{
    // Top highlight gradient (simulating glass reflection)
    auto highlightBounds = bounds.withHeight(bounds.getHeight() * 0.20f)
                                 .reduced(6 * scale, 2 * scale);

    juce::ColourGradient highlightGrad(
        juce::Colour(0x20ffffff), highlightBounds.getCentreX(), highlightBounds.getY(),
        juce::Colour(0x00ffffff), highlightBounds.getCentreX(), highlightBounds.getBottom(),
        false);
    g.setGradientFill(highlightGrad);
    g.fillRoundedRectangle(highlightBounds, 2.0f * scale);

    // Subtle edge highlight on frame
    g.setColour(juce::Colour(0x10ffffff));
    g.drawRoundedRectangle(bounds.expanded(3 * scale), 3.0f * scale, 1.0f);
}

void AnalogVUMeter::drawDecoScrews(juce::Graphics& g, const juce::Rectangle<float>& bounds, float scale)
{
    float screwRadius = 3.0f * scale;
    float margin = 8.0f * scale;

    auto drawScrew = [&](float cx, float cy)
    {
        // Screw head
        juce::ColourGradient screwGrad(
            juce::Colour(VUColors::screwChrome).brighter(0.2f), cx - screwRadius * 0.5f, cy - screwRadius * 0.5f,
            juce::Colour(VUColors::screwChrome).darker(0.2f), cx + screwRadius * 0.5f, cy + screwRadius * 0.5f,
            false);
        g.setGradientFill(screwGrad);
        g.fillEllipse(cx - screwRadius, cy - screwRadius, screwRadius * 2, screwRadius * 2);

        // Slot
        g.setColour(juce::Colour(VUColors::screwSlot));
        g.drawLine(cx - screwRadius * 0.6f, cy, cx + screwRadius * 0.6f, cy, 1.5f * scale);

        // Border
        g.setColour(juce::Colour(0xff1a1510));
        g.drawEllipse(cx - screwRadius, cy - screwRadius, screwRadius * 2, screwRadius * 2, 0.5f);
    };

    // Draw screws in corners
    drawScrew(bounds.getX() + margin, bounds.getY() + margin);
    drawScrew(bounds.getRight() - margin, bounds.getY() + margin);
    drawScrew(bounds.getX() + margin, bounds.getBottom() - margin);
    drawScrew(bounds.getRight() - margin, bounds.getBottom() - margin);
}

#include "AnalogVUMeter.h"

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
        repaint();
    }
}

void AnalogVUMeter::setLevels(float leftLevel, float rightLevel)
{
    // Convert linear to dB
    // Apply +3dB calibration offset to correct for measurement artifacts
    // Without this, the VU reads 3dB lower than the actual dBFS level
    constexpr float calibrationOffsetDB = 3.0f;

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

void AnalogVUMeter::timerCallback()
{
    // Calculate VU ballistics coefficient for 300ms integration time
    // Time constant τ = 65ms gives 99% in ~300ms (4.6 time constants)
    const float dt = 1.0f / kRefreshRateHz;
    const float vuCoeff = 1.0f - std::exp(-1000.0f * dt / kVUTimeConstantMs);

    // Process left channel
    {
        // Map target dB to normalized needle position (-20dB to +3dB range)
        float displayL = juce::jlimit(-20.0f, 3.0f, targetLevelL);
        float targetNeedleL = (displayL + 20.0f) / 23.0f;

        // Damped spring physics for realistic mechanical needle behavior
        // This creates the characteristic ~1% overshoot of real VU meters
        float displacement = targetNeedleL - needlePositionL;

        // Spring force with VU-standard response time
        float springForce = displacement * kOvershootStiffness;

        // Damping force (proportional to velocity)
        float dampingForce = -needleVelocityL * kOvershootDamping * 2.0f * std::sqrt(kOvershootStiffness);

        // Update velocity and position
        float acceleration = springForce + dampingForce;
        needleVelocityL += acceleration * dt;
        needlePositionL += needleVelocityL * dt;

        // Blend with VU integration for proper 300ms response
        // The spring provides overshoot, VU coefficient provides timing
        needlePositionL += vuCoeff * (targetNeedleL - needlePositionL) * 0.3f;

        // Clamp to valid range
        needlePositionL = juce::jlimit(0.0f, 1.0f, needlePositionL);

        // Dampen very small velocities to prevent perpetual oscillation
        displacement = targetNeedleL - needlePositionL;
        if (std::abs(needleVelocityL) < 0.001f && std::abs(displacement) < 0.001f)
            needleVelocityL = 0.0f;
    }
    // Process right channel (same logic)
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

        // Recompute displacement after all position updates for accurate threshold check
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

void AnalogVUMeter::paintSingleMeter(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                                      float needlePos, float peakLevel, const juce::String& label)
{
    // Calculate scale factor based on component size
    float scaleFactor = juce::jmin(bounds.getWidth() / 200.0f, bounds.getHeight() / 140.0f);
    scaleFactor = juce::jmax(0.5f, scaleFactor);

    // Draw outer gray frame
    g.setColour(juce::Colour(0xFFB4B4B4));
    g.fillRoundedRectangle(bounds, 3.0f * scaleFactor);

    // Draw inner darker frame
    auto innerFrame = bounds.reduced(2.0f * scaleFactor);
    g.setColour(juce::Colour(0xFF3A3A3A));
    g.fillRoundedRectangle(innerFrame, 2.0f * scaleFactor);

    // Draw classic VU meter face with warm cream color
    auto faceBounds = innerFrame.reduced(3.0f * scaleFactor);
    g.setColour(juce::Colour(0xFFF8F4E6));  // Warm cream
    g.fillRoundedRectangle(faceBounds, 2.0f * scaleFactor);

    // Set clipping region to prevent drawing outside face
    g.saveState();
    g.reduceClipRegion(faceBounds.toNearestInt());

    // Set up meter geometry
    auto centreX = faceBounds.getCentreX();
    auto pivotY = faceBounds.getBottom() - (3 * scaleFactor);

    // Calculate needle length
    auto maxHeightForText = faceBounds.getHeight() * 0.88f;
    auto maxWidthRadius = faceBounds.getWidth() * 0.49f;
    auto needleLength = juce::jmin(maxWidthRadius, maxHeightForText);

    // VU scale angles - wider sweep for authentic look
    const float scaleStart = -2.7f;
    const float scaleEnd = -0.44f;

    // Font setup for scale markings
    float baseFontSize = juce::jmax(8.0f, 11.0f * scaleFactor);
    g.setFont(juce::Font(baseFontSize));

    // Draw scale markings
    const float dbValues[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    const int numDbValues = 11;

    for (int i = 0; i < numDbValues; ++i)
    {
        float db = dbValues[i];
        float normalizedP = (db + 20.0f) / 23.0f;
        float angle = scaleStart + normalizedP * (scaleEnd - scaleStart);

        bool isMajor = (db == -20 || db == -10 || db == -7 || db == -5 || db == -3 ||
                       db == -2 || db == -1 || db == 0 || db == 1 || db == 3);
        bool showText = (db == -20 || db == -10 || db == -5 || db == 0 || db == 3);

        // Draw tick marks
        auto tickLength = isMajor ? (8.0f * scaleFactor) : (5.0f * scaleFactor);
        auto tickRadius = needleLength * 0.95f;
        auto x1 = centreX + tickRadius * std::cos(angle);
        auto y1 = pivotY + tickRadius * std::sin(angle);
        auto x2 = centreX + (tickRadius + tickLength) * std::cos(angle);
        auto y2 = pivotY + (tickRadius + tickLength) * std::sin(angle);

        // Classic VU meter colors - red zone at 0 and above
        if (db >= 0)
            g.setColour(juce::Colour(0xFFD42C2C));  // Red
        else
            g.setColour(juce::Colour(0xFF2A2A2A));  // Dark gray

        g.drawLine(x1, y1, x2, y2, isMajor ? 1.5f * scaleFactor : 1.0f * scaleFactor);

        // Draw text labels
        if (showText)
        {
            auto textRadius = needleLength * 0.72f;
            auto textX = centreX + textRadius * std::cos(angle);
            auto textY = pivotY + textRadius * std::sin(angle);

            float textBoxWidth = 24 * scaleFactor;
            float textBoxHeight = 12 * scaleFactor;

            // Keep text within bounds
            float minY = faceBounds.getY() + (4 * scaleFactor);
            if (textY - textBoxHeight/2 < minY)
                textY = minY + textBoxHeight/2;

            juce::String dbText;
            if (db == 0)
                dbText = "0";
            else if (db > 0)
                dbText = "+" + juce::String((int)db);
            else
                dbText = juce::String((int)db);

            if (db >= 0)
                g.setColour(juce::Colour(0xFFD42C2C));
            else
                g.setColour(juce::Colour(0xFF2A2A2A));

            g.drawText(dbText, textX - textBoxWidth/2, textY - textBoxHeight/2,
                      textBoxWidth, textBoxHeight, juce::Justification::centred);
        }
    }

    // Draw VU text (or channel label in stereo mode)
    g.setColour(juce::Colour(0xFF2A2A2A));
    float vuFontSize = juce::jmax(10.0f, 14.0f * scaleFactor);
    g.setFont(juce::Font(vuFontSize).withTypefaceStyle("Bold"));
    float vuY = pivotY - (needleLength * 0.45f);
    g.drawText(label.isEmpty() ? "VU" : label, centreX - 15 * scaleFactor, vuY,
              30 * scaleFactor, 16 * scaleFactor, juce::Justification::centred);

    // Draw needle
    float needleAngle = scaleStart + needlePos * (scaleEnd - scaleStart);

    g.setColour(juce::Colour(0xFFCC3333));  // Classic VU red
    juce::Path needle;
    needle.startNewSubPath(centreX, pivotY);
    needle.lineTo(centreX + needleLength * 0.96f * std::cos(needleAngle),
                  pivotY + needleLength * 0.96f * std::sin(needleAngle));
    g.strokePath(needle, juce::PathStrokeType(2.0f * scaleFactor));

    // Draw needle pivot
    float pivotRadius = 3 * scaleFactor;
    g.setColour(juce::Colour(0xFF000000));
    g.fillEllipse(centreX - pivotRadius, pivotY - pivotRadius,
                  pivotRadius * 2, pivotRadius * 2);

    // Restore graphics state
    g.restoreState();

    // Subtle glass reflection
    auto glassBounds = faceBounds;
    auto highlightBounds = glassBounds.removeFromTop(glassBounds.getHeight() * 0.15f)
                                      .reduced(8 * scaleFactor, 3 * scaleFactor);
    juce::ColourGradient highlightGradient(
        juce::Colour(0x18FFFFFF),
        highlightBounds.getCentreX(), highlightBounds.getY(),
        juce::Colour(0x00FFFFFF),
        highlightBounds.getCentreX(), highlightBounds.getBottom(),
        false
    );
    g.setGradientFill(highlightGradient);
    g.fillRoundedRectangle(highlightBounds, 2.0f * scaleFactor);
}

void AnalogVUMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (stereoMode)
    {
        // Stereo mode: Draw two VU meters side by side
        float gap = 8.0f;
        float meterWidth = (bounds.getWidth() - gap) / 2.0f;

        // Left meter
        auto leftBounds = bounds.withWidth(meterWidth);
        paintSingleMeter(g, leftBounds, needlePositionL, peakLevelL, "L");

        // Right meter
        auto rightBounds = bounds.withX(bounds.getX() + meterWidth + gap).withWidth(meterWidth);
        paintSingleMeter(g, rightBounds, needlePositionR, peakLevelR, "R");
    }
    else
    {
        // Mono mode: Draw single VU meter showing max of L/R
        float avgNeedlePos = juce::jmax(needlePositionL, needlePositionR);
        float avgPeakLevel = juce::jmax(peakLevelL, peakLevelR);
        paintSingleMeter(g, bounds, avgNeedlePos, avgPeakLevel, "VU");
    }
}

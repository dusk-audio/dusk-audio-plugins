#include "AnalogVUMeter.h"

AnalogVUMeter::AnalogVUMeter()
{
    startTimerHz(60);
}

AnalogVUMeter::~AnalogVUMeter()
{
    stopTimer();
}

void AnalogVUMeter::setLevels(float leftLevel, float rightLevel)
{
    // Convert linear to dB (no calibration offset - VU shows dBFS directly)
    float dbL = 20.0f * std::log10(std::max(0.001f, leftLevel));
    float dbR = 20.0f * std::log10(std::max(0.001f, rightLevel));

    targetLevelL = dbL;
    targetLevelR = dbR;

    // Debug: Log what the VU meter is receiving
    static int debugCounter = 0;
    if (++debugCounter > 60)  // Log every ~1 second
    {
        debugCounter = 0;
        juce::File logFile("/tmp/tapemachine_vu_display.txt");
        juce::String logText;
        logText << "=== VU METER DISPLAY ===" << juce::newLine;
        logText << "Received Linear L: " << juce::String(leftLevel, 4) << juce::newLine;
        logText << "Received Linear R: " << juce::String(rightLevel, 4) << juce::newLine;
        logText << "Converted to dB L: " << juce::String(dbL, 2) << " dB" << juce::newLine;
        logText << "Converted to dB R: " << juce::String(dbR, 2) << " dB" << juce::newLine;
        logText << "Target Level L:    " << juce::String(targetLevelL, 2) << " dB" << juce::newLine;
        logText << "Target Level R:    " << juce::String(targetLevelR, 2) << " dB" << juce::newLine;
        logText << juce::newLine;
        logFile.appendText(logText);
    }

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
    const float frameRate = 60.0f;

    // VU meter ballistics - smooth movement
    const float smoothing = 0.15f;
    currentLevelL += (targetLevelL - currentLevelL) * smoothing;
    currentLevelR += (targetLevelR - currentLevelR) * smoothing;

    // Map dB to needle position (-20dB to +3dB range)
    float displayL = juce::jlimit(-20.0f, 3.0f, currentLevelL);
    float displayR = juce::jlimit(-20.0f, 3.0f, currentLevelR);

    // Map to needle position: -20dB = 0.13, 0dB = 0.87, +3dB = 1.0
    float normalizedL = (displayL + 20.0f) / 23.0f;
    float normalizedR = (displayR + 20.0f) / 23.0f;

    float targetNeedleL = juce::jlimit(0.0f, 1.0f, normalizedL);
    float targetNeedleR = juce::jlimit(0.0f, 1.0f, normalizedR);

    // Smooth needle movement
    const float needleSmoothing = 0.25f;
    needlePositionL += (targetNeedleL - needlePositionL) * needleSmoothing;
    needlePositionR += (targetNeedleR - needlePositionR) * needleSmoothing;

    // Peak hold decay
    if (peakHoldTimeL > 0)
    {
        peakHoldTimeL -= 1.0f / frameRate;
        if (peakHoldTimeL <= 0)
            peakLevelL = currentLevelL;
    }
    if (peakHoldTimeR > 0)
    {
        peakHoldTimeR -= 1.0f / frameRate;
        if (peakHoldTimeR <= 0)
            peakLevelR = currentLevelR;
    }

    repaint();
}

void AnalogVUMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Calculate scale factor based on component size
    float scaleFactor = juce::jmin(bounds.getWidth() / 450.0f, bounds.getHeight() / 180.0f);
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

    // Calculate needle length to fill the meter
    auto maxHeightForText = faceBounds.getHeight() * 0.88f;
    auto maxWidthRadius = faceBounds.getWidth() * 0.49f;
    auto needleLength = juce::jmin(maxWidthRadius, maxHeightForText);

    // VU scale angles - wider sweep for authentic look
    const float scaleStart = -2.7f;
    const float scaleEnd = -0.44f;

    // Scale arc removed - was creating dark line that obscured markings

    // Font setup for scale markings
    float baseFontSize = juce::jmax(10.0f, 14.0f * scaleFactor);
    g.setFont(juce::Font(baseFontSize));

    // Draw scale markings
    const float dbValues[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    const int numDbValues = 11;

    for (int i = 0; i < numDbValues; ++i)
    {
        float db = dbValues[i];
        float normalizedPos = (db + 20.0f) / 23.0f;
        float angle = scaleStart + normalizedPos * (scaleEnd - scaleStart);

        bool isMajor = (db == -20 || db == -10 || db == -7 || db == -5 || db == -3 ||
                       db == -2 || db == -1 || db == 0 || db == 1 || db == 3);
        bool showText = (db == -20 || db == -10 || db == -5 || db == -3 ||
                        db == 0 || db == 3);

        // Draw tick marks
        auto tickLength = isMajor ? (10.0f * scaleFactor) : (6.0f * scaleFactor);
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

        g.drawLine(x1, y1, x2, y2, isMajor ? 2.0f * scaleFactor : 1.0f * scaleFactor);

        // Draw text labels
        if (showText)
        {
            auto textRadius = needleLength * 0.72f;
            auto textX = centreX + textRadius * std::cos(angle);
            auto textY = pivotY + textRadius * std::sin(angle);

            float textBoxWidth = 30 * scaleFactor;
            float textBoxHeight = 15 * scaleFactor;

            // Keep text within bounds
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

            // Text colors match tick colors
            if (db >= 0)
                g.setColour(juce::Colour(0xFFD42C2C));
            else
                g.setColour(juce::Colour(0xFF2A2A2A));

            g.drawText(dbText, textX - textBoxWidth/2, textY - textBoxHeight/2,
                      textBoxWidth, textBoxHeight, juce::Justification::centred);
        }
    }

    // Removed percentage markings - they were cluttering the display

    // Draw VU text
    g.setColour(juce::Colour(0xFF2A2A2A));
    float vuFontSize = juce::jmax(14.0f, 18.0f * scaleFactor);
    g.setFont(juce::Font(vuFontSize).withTypefaceStyle("Regular"));
    float vuY = pivotY - (needleLength * 0.5f);
    g.drawText("VU", centreX - 20 * scaleFactor, vuY,
              40 * scaleFactor, 20 * scaleFactor, juce::Justification::centred);

    // Draw single needle showing max of L/R (works for both mono and stereo)
    float avgNeedlePos = juce::jmax(needlePositionL, needlePositionR);
    float needleAngle = scaleStart + avgNeedlePos * (scaleEnd - scaleStart);

    // Debug: Log needle position calculation
    static int paintDebugCounter = 0;
    if (++paintDebugCounter > 60)
    {
        paintDebugCounter = 0;
        juce::File logFile("/tmp/tapemachine_vu_needle.txt");
        juce::String logText;
        logText << "=== NEEDLE CALCULATION ===" << juce::newLine;
        logText << "currentLevelL:   " << juce::String(currentLevelL, 2) << " dB" << juce::newLine;
        logText << "currentLevelR:   " << juce::String(currentLevelR, 2) << " dB" << juce::newLine;
        logText << "targetLevelL:    " << juce::String(targetLevelL, 2) << " dB" << juce::newLine;
        logText << "targetLevelR:    " << juce::String(targetLevelR, 2) << " dB" << juce::newLine;
        logText << "needlePositionL: " << juce::String(needlePositionL, 4) << juce::newLine;
        logText << "needlePositionR: " << juce::String(needlePositionR, 4) << juce::newLine;
        logText << "avgNeedlePos:    " << juce::String(avgNeedlePos, 4) << " (should be avg of L and R)" << juce::newLine;
        logText << "needleAngle:     " << juce::String(needleAngle, 4) << " rad" << juce::newLine;
        logText << juce::newLine;
        logFile.appendText(logText);
    }

    g.setColour(juce::Colour(0xFFCC3333));  // Classic VU red
    juce::Path needle;
    needle.startNewSubPath(centreX, pivotY);
    needle.lineTo(centreX + needleLength * 0.96f * std::cos(needleAngle),
                  pivotY + needleLength * 0.96f * std::sin(needleAngle));
    g.strokePath(needle, juce::PathStrokeType(2.5f * scaleFactor));

    // Draw needle pivot
    float pivotRadius = 4 * scaleFactor;
    g.setColour(juce::Colour(0xFF000000));
    g.fillEllipse(centreX - pivotRadius, pivotY - pivotRadius,
                  pivotRadius * 2, pivotRadius * 2);


    // Restore graphics state
    g.restoreState();

    // Subtle glass reflection
    auto glassBounds = faceBounds;
    auto highlightBounds = glassBounds.removeFromTop(glassBounds.getHeight() * 0.2f)
                                      .reduced(10 * scaleFactor, 5 * scaleFactor);
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
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
    // Convert linear to dB
    float dbL = 20.0f * std::log10(std::max(0.001f, leftLevel));
    float dbR = 20.0f * std::log10(std::max(0.001f, rightLevel));

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

    // Draw scale arc
    g.setColour(juce::Colour(0xFF1A1A1A).withAlpha(0.7f));
    juce::Path scaleArc;
    scaleArc.addCentredArc(centreX, pivotY, needleLength * 0.95f, needleLength * 0.95f,
                          0, scaleStart, scaleEnd, true);
    g.strokePath(scaleArc, juce::PathStrokeType(2.0f * scaleFactor));

    // Font setup for scale markings
    float baseFontSize = juce::jmax(10.0f, 14.0f * scaleFactor);
    g.setFont(juce::Font(juce::FontOptions(baseFontSize)));

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
    g.setFont(juce::Font(juce::FontOptions(vuFontSize)).withTypefaceStyle("Regular"));
    float vuY = pivotY - (needleLength * 0.5f);
    g.drawText("VU", centreX - 20 * scaleFactor, vuY,
              40 * scaleFactor, 20 * scaleFactor, juce::Justification::centred);

    // Draw LEFT needle (red/orange)
    float needleAngleL = scaleStart + needlePositionL * (scaleEnd - scaleStart);
    g.setColour(juce::Colour(0xFFCC3333));  // Red-orange for left
    juce::Path needleL;
    needleL.startNewSubPath(centreX, pivotY);
    needleL.lineTo(centreX + needleLength * 0.96f * std::cos(needleAngleL),
                  pivotY + needleLength * 0.96f * std::sin(needleAngleL));
    g.strokePath(needleL, juce::PathStrokeType(2.0f * scaleFactor));

    // Draw RIGHT needle (darker red)
    float needleAngleR = scaleStart + needlePositionR * (scaleEnd - scaleStart);
    g.setColour(juce::Colour(0xFF992222));  // Darker red for right
    juce::Path needleR;
    needleR.startNewSubPath(centreX, pivotY);
    needleR.lineTo(centreX + needleLength * 0.96f * std::cos(needleAngleR),
                  pivotY + needleLength * 0.96f * std::sin(needleAngleR));
    g.strokePath(needleR, juce::PathStrokeType(1.8f * scaleFactor));

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
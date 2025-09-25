#include "VUMeter.h"

VUMeter::VUMeter()
{
    startTimerHz(30);
}

VUMeter::~VUMeter()
{
    stopTimer();
}

void VUMeter::setLevels(float leftLevel, float rightLevel)
{
    targetLevelL = juce::jlimit(0.0f, 1.0f, leftLevel);
    targetLevelR = juce::jlimit(0.0f, 1.0f, rightLevel);
    // Needle should go from -135° (left, no signal) to -45° (right, full signal)
    targetAngleL = -135.0f + targetLevelL * 90.0f;
    targetAngleR = -135.0f + targetLevelR * 90.0f;
}

void VUMeter::timerCallback()
{
    // Smooth needle movement
    const float smoothing = 0.15f;
    needleAngleL += (targetAngleL - needleAngleL) * smoothing;
    needleAngleR += (targetAngleR - needleAngleR) * smoothing;
    levelL += (targetLevelL - levelL) * smoothing;
    levelR += (targetLevelR - levelR) * smoothing;

    if (std::abs(needleAngleL - targetAngleL) > 0.01f ||
        std::abs(needleAngleR - targetAngleR) > 0.01f)
    {
        repaint();
    }
}

void VUMeter::paint(juce::Graphics& g)
{
    if (isVintage)
        drawVintageVUMeter(g);
    else
        drawModernVUMeter(g);
}

void VUMeter::resized()
{
}

void VUMeter::drawVintageVUMeter(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto meterBounds = bounds;

    // Black meter background
    g.setColour(juce::Colour(20, 22, 18));
    g.fillRoundedRectangle(meterBounds, 8.0f);

    // Meter face - dark with subtle gradient
    juce::ColourGradient gradient(juce::Colour(35, 38, 30), meterBounds.getCentre(),
                                   juce::Colour(20, 22, 18), meterBounds.getTopLeft(), true);
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(meterBounds.reduced(5), 6.0f);

    // Move center to bottom of the box for proper VU meter positioning
    auto centre = juce::Point<float>(meterBounds.getCentreX(), meterBounds.getBottom() - 20);
    // Make the radius MUCH wider to fill horizontal space
    auto radius = juce::jmin(meterBounds.getWidth() * 0.55f, meterBounds.getHeight() * 0.9f);

    // Don't draw the scale arc - just use tick marks
    float startAngle = juce::degreesToRadians(-135.0f);
    float endAngle = juce::degreesToRadians(-45.0f);

    // Scale markings - larger font for wider meter
    g.setFont(juce::Font("Arial", 14.0f, juce::Font::plain));
    g.setColour(juce::Colour(240, 230, 210));

    for (int i = 0; i <= 10; ++i)
    {
        float angle = startAngle + (endAngle - startAngle) * (i / 10.0f);
        float tickLength = (i % 5 == 0) ? 30.0f : 18.0f;

        float x1 = centre.x + (radius - tickLength) * std::cos(angle);
        float y1 = centre.y + (radius - tickLength) * std::sin(angle);
        float x2 = centre.x + radius * std::cos(angle);
        float y2 = centre.y + radius * std::sin(angle);

        g.setColour(juce::Colour(200, 190, 170));
        g.drawLine(x1, y1, x2, y2, (i % 5 == 0) ? 2.0f : 1.0f);

        // Numbers
        if (i % 2 == 0)
        {
            float textX = centre.x + (radius * 0.8f) * std::cos(angle);
            float textY = centre.y + (radius * 0.8f) * std::sin(angle);

            juce::String text;
            // VU meter scale: -20, -10, -7, -5, -3, 0, +1, +2, +3
            switch(i) {
                case 0: text = "-20"; break;
                case 2: text = "-10"; break;
                case 4: text = "-5"; break;
                case 6: text = "0"; break;
                case 8: text = "+2"; break;
                case 10: text = "+3"; break;
                default: continue;
            }

            g.setColour(juce::Colour(250, 240, 220));
            g.setFont(15.0f);
            g.drawText(text, juce::Rectangle<float>(textX - 18, textY - 10, 36, 20),
                       juce::Justification::centred);
        }
    }

    // Red zone - draw only in the red area
    float redStart = juce::degreesToRadians(-65.0f);
    float redEnd = juce::degreesToRadians(-45.0f);

    // Draw red zone as individual tick marks instead of arc
    for (float angle = redStart; angle <= redEnd; angle += 0.05f)
    {
        float x1 = centre.x + (radius - 2) * std::cos(angle);
        float y1 = centre.y + (radius - 2) * std::sin(angle);
        float x2 = centre.x + radius * std::cos(angle);
        float y2 = centre.y + radius * std::sin(angle);

        g.setColour(juce::Colour(200, 50, 30));
        g.drawLine(x1, y1, x2, y2, 2.0f);
    }

    // STEREO VU text removed - not needed

    // "PEAK LEVEL" text at top of the meter arc
    g.setFont(10.0f);
    g.drawText("PEAK LEVEL", juce::Rectangle<float>(centre.x - 50, meterBounds.getY() + 5, 100, 20),
               juce::Justification::centred);

    // Draw LEFT needle
    float needleRadiansL = juce::degreesToRadians(needleAngleL);
    float needleXL = centre.x + radius * 0.9f * std::cos(needleRadiansL);
    float needleYL = centre.y + radius * 0.9f * std::sin(needleRadiansL);

    // Left needle shadow
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.drawLine(centre.x + 1, centre.y + 1, needleXL + 1, needleYL + 1, 2.5f);

    // Left needle - slightly brighter red/orange
    g.setColour(needleColourL);
    g.drawLine(centre.x, centre.y, needleXL, needleYL, 2.5f);

    // Draw RIGHT needle
    float needleRadiansR = juce::degreesToRadians(needleAngleR);
    float needleXR = centre.x + radius * 0.9f * std::cos(needleRadiansR);
    float needleYR = centre.y + radius * 0.9f * std::sin(needleRadiansR);

    // Right needle shadow
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.drawLine(centre.x + 1, centre.y + 1, needleXR + 1, needleYR + 1, 2.5f);

    // Right needle - slightly darker red
    g.setColour(needleColourR);
    g.drawLine(centre.x, centre.y, needleXR, needleYR, 2.5f);

    // Needle hub at bottom - brass colored (drawn on top of both needles)
    g.setColour(juce::Colour(140, 120, 80));
    g.fillEllipse(centre.x - 10, centre.y - 10, 20, 20);
    g.setColour(juce::Colour(80, 70, 50));
    g.drawEllipse(centre.x - 10, centre.y - 10, 20, 20, 2.0f);
    g.setColour(juce::Colour(180, 160, 120));
    g.fillEllipse(centre.x - 5, centre.y - 5, 10, 10);

    // L/R indicators removed - not needed for stereo VU meter
}

void VUMeter::drawModernVUMeter(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(30, 30, 35));
    g.fillRoundedRectangle(bounds, 5.0f);

    // Split into two sections for L and R
    auto leftBounds = bounds.removeFromTop(bounds.getHeight() * 0.5f);
    auto rightBounds = bounds;

    // Draw left channel bars
    int numBars = 20;
    float barWidth = leftBounds.getWidth() * 0.8f / numBars;
    float barHeight = leftBounds.getHeight() * 0.3f;
    float startX = leftBounds.getWidth() * 0.1f;
    float yL = leftBounds.getCentreY() - barHeight * 0.5f;

    for (int i = 0; i < numBars; ++i)
    {
        float barLevel = i / float(numBars - 1);
        float x = startX + i * (barWidth * 1.1f);

        juce::Colour barColour;
        if (barLevel < 0.6f)
            barColour = juce::Colours::green;
        else if (barLevel < 0.8f)
            barColour = juce::Colours::yellow;
        else
            barColour = juce::Colours::red;

        if (barLevel <= levelL)
        {
            g.setColour(barColour);
        }
        else
        {
            g.setColour(barColour.withAlpha(0.2f));
        }

        g.fillRoundedRectangle(x, yL, barWidth, barHeight, 2.0f);
    }

    // Draw right channel bars
    float yR = rightBounds.getCentreY() - barHeight * 0.5f;
    for (int i = 0; i < numBars; ++i)
    {
        float barLevel = i / float(numBars - 1);
        float x = startX + i * (barWidth * 1.1f);

        juce::Colour barColour;
        if (barLevel < 0.6f)
            barColour = juce::Colours::green;
        else if (barLevel < 0.8f)
            barColour = juce::Colours::yellow;
        else
            barColour = juce::Colours::red;

        if (barLevel <= levelR)
        {
            g.setColour(barColour);
        }
        else
        {
            g.setColour(barColour.withAlpha(0.2f));
        }

        g.fillRoundedRectangle(x, yR, barWidth, barHeight, 2.0f);
    }

    // Level text
    g.setColour(juce::Colours::lightgrey);
    g.setFont(10.0f);

    float dbL = 20.0f * std::log10(std::max(0.001f, levelL));
    g.drawText("L: " + juce::String(dbL, 1) + " dB", leftBounds,
               juce::Justification::centredRight);

    float dbR = 20.0f * std::log10(std::max(0.001f, levelR));
    g.drawText("R: " + juce::String(dbR, 1) + " dB", rightBounds,
               juce::Justification::centredRight);
}
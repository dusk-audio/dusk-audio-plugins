#include "VUMeter.h"

VUMeter::VUMeter()
{
    startTimerHz(30);
}

VUMeter::~VUMeter()
{
    stopTimer();
}

void VUMeter::setLevel(float newLevel)
{
    targetLevel = juce::jlimit(0.0f, 1.0f, newLevel);
    targetAngle = -45.0f + targetLevel * 90.0f;
}

void VUMeter::setVintageMode(bool vintageMode)
{
    if (isVintage != vintageMode)
    {
        isVintage = vintageMode;
        repaint();
    }
}

void VUMeter::timerCallback()
{
    // Smooth needle movement
    const float smoothing = 0.15f;
    needleAngle += (targetAngle - needleAngle) * smoothing;
    level += (targetLevel - level) * smoothing;

    if (std::abs(needleAngle - targetAngle) > 0.01f)
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
    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    auto meterBounds = bounds.withSizeKeepingCentre(size, size);

    // Black meter background
    g.setColour(juce::Colour(20, 22, 18));
    g.fillRoundedRectangle(meterBounds, 8.0f);

    // Meter face - dark with subtle gradient
    juce::ColourGradient gradient(juce::Colour(35, 38, 30), meterBounds.getCentre(),
                                   juce::Colour(20, 22, 18), meterBounds.getTopLeft(), true);
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(meterBounds.reduced(5), 6.0f);

    auto centre = meterBounds.getCentre();
    auto radius = size * 0.4f;  // Good size for visibility

    // Don't draw the scale arc - just use tick marks
    float startAngle = juce::degreesToRadians(-135.0f);
    float endAngle = juce::degreesToRadians(-45.0f);

    // Scale markings
    g.setFont(juce::Font("Arial", 8.0f, juce::Font::plain));

    for (int i = 0; i <= 10; ++i)
    {
        float angle = startAngle + (endAngle - startAngle) * (i / 10.0f);
        float tickLength = (i % 5 == 0) ? 12.0f : 6.0f;

        float x1 = centre.x + (radius - tickLength) * std::cos(angle);
        float y1 = centre.y + (radius - tickLength) * std::sin(angle);
        float x2 = centre.x + radius * std::cos(angle);
        float y2 = centre.y + radius * std::sin(angle);

        g.setColour(juce::Colour(200, 190, 170));
        g.drawLine(x1, y1, x2, y2, (i % 5 == 0) ? 2.0f : 1.0f);

        // Numbers
        if (i % 2 == 0)
        {
            float textX = centre.x + (radius - 25) * std::cos(angle);
            float textY = centre.y + (radius - 25) * std::sin(angle);

            juce::String text;
            if (i <= 6)
                text = juce::String(i - 6);
            else
                text = "+" + juce::String(i - 6);

            g.setColour(juce::Colour(200, 190, 170));
            g.drawText(text, juce::Rectangle<float>(textX - 10, textY - 6, 20, 12),
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

    // VU label at bottom
    g.setColour(juce::Colour(200, 190, 170));
    g.setFont(juce::Font("Arial", 12.0f, juce::Font::bold));
    auto vuLabelBounds = bounds.removeFromBottom(20);
    g.drawText("VU", vuLabelBounds, juce::Justification::centred);

    // "PEAK LEVEL" text at top
    g.setFont(9.0f);
    auto peakLabelBounds = bounds.removeFromTop(15);
    g.drawText("PEAK LEVEL", peakLabelBounds, juce::Justification::centred);

    // Draw needle (without duplicate shadow)
    float needleRadians = juce::degreesToRadians(needleAngle);
    float needleX = centre.x + radius * 0.85f * std::cos(needleRadians);
    float needleY = centre.y + radius * 0.85f * std::sin(needleRadians);

    // Single shadow
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.drawLine(centre.x + 1, centre.y + 1, needleX + 1, needleY + 1, 3.0f);

    // Needle - red/orange color
    g.setColour(juce::Colour(220, 80, 40));
    g.drawLine(centre.x, centre.y, needleX, needleY, 2.5f);

    // Needle hub - brass colored
    g.setColour(juce::Colour(140, 120, 80));
    g.fillEllipse(centre.x - 6, centre.y - 6, 12, 12);
    g.setColour(juce::Colour(80, 70, 50));
    g.drawEllipse(centre.x - 6, centre.y - 6, 12, 12, 1.0f);
    g.setColour(juce::Colour(180, 160, 120));
    g.fillEllipse(centre.x - 3, centre.y - 3, 6, 6);
}

void VUMeter::drawModernVUMeter(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(30, 30, 35));
    g.fillRoundedRectangle(bounds, 5.0f);

    // LED-style bars
    int numBars = 20;
    float barWidth = bounds.getWidth() * 0.8f / numBars;
    float barHeight = bounds.getHeight() * 0.3f;
    float startX = bounds.getWidth() * 0.1f;
    float y = bounds.getCentreY() - barHeight * 0.5f;

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

        if (barLevel <= level)
        {
            g.setColour(barColour);
        }
        else
        {
            g.setColour(barColour.withAlpha(0.2f));
        }

        g.fillRoundedRectangle(x, y, barWidth, barHeight, 2.0f);
    }

    // Level text
    g.setColour(juce::Colours::lightgrey);
    g.setFont(10.0f);
    float db = 20.0f * std::log10(std::max(0.001f, level));
    g.drawText(juce::String(db, 1) + " dB", bounds.removeFromBottom(20),
               juce::Justification::centred);
}
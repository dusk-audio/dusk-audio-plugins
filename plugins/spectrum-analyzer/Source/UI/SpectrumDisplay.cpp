#include "SpectrumDisplay.h"

//==============================================================================
SpectrumDisplay::SpectrumDisplay()
{
    currentMagnitudes.fill(-100.0f);
    currentPeakHold.fill(-100.0f);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void SpectrumDisplay::resized()
{
    // Leave space for labels
    const int leftMargin = 40;
    const int rightMargin = 10;
    const int topMargin = 10;
    const int bottomMargin = 25;

    displayArea = getLocalBounds()
        .withTrimmedLeft(leftMargin)
        .withTrimmedRight(rightMargin)
        .withTrimmedTop(topMargin)
        .withTrimmedBottom(bottomMargin)
        .toFloat();
}

//==============================================================================
void SpectrumDisplay::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xff1a1a1a));

    // Draw elements
    drawGrid(g);
    drawSpectrum(g);

    if (showPeakHold)
        drawPeakHold(g);

    if (isHovering)
        drawHoverInfo(g);

    // Border
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawRect(displayArea, 1.0f);
}

//==============================================================================
void SpectrumDisplay::drawGrid(juce::Graphics& g)
{
    g.setColour(gridColor);

    // Vertical lines at key frequencies
    const std::array<float, 10> freqLines = {
        50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
        2000.0f, 5000.0f, 10000.0f, 20000.0f, 20.0f
    };

    for (float freq : freqLines)
    {
        float x = getXForFrequency(freq);
        if (x >= displayArea.getX() && x <= displayArea.getRight())
        {
            g.drawVerticalLine(static_cast<int>(x), displayArea.getY(), displayArea.getBottom());
        }
    }

    // Horizontal lines at dB intervals
    float dbStep = (maxDisplayDB - minDisplayDB) > 48.0f ? 12.0f : 6.0f;
    for (float dB = minDisplayDB; dB <= maxDisplayDB; dB += dbStep)
    {
        float y = getYForDB(dB);
        g.drawHorizontalLine(static_cast<int>(y), displayArea.getX(), displayArea.getRight());
    }

    // Draw labels
    g.setColour(labelColor);
    g.setFont(10.0f);

    // Frequency labels
    const std::array<std::pair<float, const char*>, 8> freqLabels = {{
        {20.0f, "20"}, {50.0f, "50"}, {100.0f, "100"}, {500.0f, "500"},
        {1000.0f, "1k"}, {5000.0f, "5k"}, {10000.0f, "10k"}, {20000.0f, "20k"}
    }};

    for (const auto& [freq, label] : freqLabels)
    {
        float x = getXForFrequency(freq);
        g.drawText(label,
            static_cast<int>(x - 15), static_cast<int>(displayArea.getBottom() + 3),
            30, 20, juce::Justification::centredTop);
    }

    // dB labels
    for (float dB = minDisplayDB; dB <= maxDisplayDB; dB += dbStep)
    {
        float y = getYForDB(dB);
        juce::String label = juce::String(static_cast<int>(dB));
        if (dB > 0) label = "+" + label;
        g.drawText(label,
            0, static_cast<int>(y - 8),
            35, 16, juce::Justification::centredRight);
    }
}

//==============================================================================
void SpectrumDisplay::drawSpectrum(juce::Graphics& g)
{
    juce::Path spectrumPath = createSpectrumPath();

    if (spectrumPath.isEmpty())
        return;

    // Create gradient fill
    juce::ColourGradient gradient(
        spectrumColor.withAlpha(0.6f), displayArea.getX(), displayArea.getY(),
        spectrumColor.withAlpha(0.1f), displayArea.getX(), displayArea.getBottom(),
        false);

    // Fill path
    juce::Path fillPath = spectrumPath;
    fillPath.lineTo(displayArea.getRight(), displayArea.getBottom());
    fillPath.lineTo(displayArea.getX(), displayArea.getBottom());
    fillPath.closeSubPath();

    g.setGradientFill(gradient);
    g.fillPath(fillPath);

    // Stroke outline
    g.setColour(spectrumColor);
    g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));
}

void SpectrumDisplay::drawPeakHold(juce::Graphics& g)
{
    juce::Path peakPath;
    bool started = false;

    for (int i = 0; i < NUM_BINS; ++i)
    {
        float x = displayArea.getX() + (static_cast<float>(i) / static_cast<float>(NUM_BINS - 1)) * displayArea.getWidth();
        float y = getYForDB(currentPeakHold[i]);
        y = juce::jlimit(displayArea.getY(), displayArea.getBottom(), y);

        if (!started)
        {
            peakPath.startNewSubPath(x, y);
            started = true;
        }
        else
        {
            peakPath.lineTo(x, y);
        }
    }

    g.setColour(peakHoldColor);
    g.strokePath(peakPath, juce::PathStrokeType(1.0f));
}

void SpectrumDisplay::drawHoverInfo(juce::Graphics& g)
{
    float freq = getFrequencyAtX(hoverPosition.x);
    float dB = getDBAtY(hoverPosition.y);

    // Find actual magnitude at this frequency
    int bin = static_cast<int>((hoverPosition.x - displayArea.getX()) /
        displayArea.getWidth() * static_cast<float>(NUM_BINS - 1));
    bin = juce::jlimit(0, NUM_BINS - 1, bin);
    float actualDB = currentMagnitudes[bin];

    // Format frequency
    juce::String freqStr;
    if (freq >= 1000.0f)
        freqStr = juce::String(freq / 1000.0f, 2) + " kHz";
    else
        freqStr = juce::String(static_cast<int>(freq)) + " Hz";

    juce::String dbStr = juce::String(actualDB, 1) + " dB";
    juce::String infoStr = freqStr + "  " + dbStr;

    // Draw crosshair
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.drawVerticalLine(static_cast<int>(hoverPosition.x),
        displayArea.getY(), displayArea.getBottom());
    g.drawHorizontalLine(static_cast<int>(getYForDB(actualDB)),
        displayArea.getX(), displayArea.getRight());

    // Draw info box
    g.setFont(11.0f);
    int textWidth = g.getCurrentFont().getStringWidth(infoStr) + 10;

    float boxX = hoverPosition.x + 10;
    float boxY = hoverPosition.y - 25;

    // Keep box in bounds
    if (boxX + textWidth > displayArea.getRight())
        boxX = hoverPosition.x - textWidth - 10;
    if (boxY < displayArea.getY())
        boxY = hoverPosition.y + 10;

    g.setColour(juce::Colour(0xe0202020));
    g.fillRoundedRectangle(boxX, boxY, static_cast<float>(textWidth), 20.0f, 3.0f);

    g.setColour(juce::Colours::white);
    g.drawText(infoStr, static_cast<int>(boxX), static_cast<int>(boxY),
        textWidth, 20, juce::Justification::centred);
}

//==============================================================================
juce::Path SpectrumDisplay::createSpectrumPath() const
{
    juce::Path path;
    bool started = false;

    for (int i = 0; i < NUM_BINS; ++i)
    {
        float x = displayArea.getX() + (static_cast<float>(i) / static_cast<float>(NUM_BINS - 1)) * displayArea.getWidth();
        float y = getYForDB(currentMagnitudes[i]);
        y = juce::jlimit(displayArea.getY(), displayArea.getBottom(), y);

        if (!started)
        {
            path.startNewSubPath(x, y);
            started = true;
        }
        else
        {
            path.lineTo(x, y);
        }
    }

    return path;
}

//==============================================================================
void SpectrumDisplay::updateMagnitudes(const std::array<float, NUM_BINS>& magnitudes)
{
    currentMagnitudes = magnitudes;
    repaint();
}

void SpectrumDisplay::updatePeakHold(const std::array<float, NUM_BINS>& peakHold)
{
    currentPeakHold = peakHold;
}

void SpectrumDisplay::setDisplayRange(float minDB, float maxDB)
{
    minDisplayDB = minDB;
    maxDisplayDB = maxDB;
    repaint();
}

//==============================================================================
void SpectrumDisplay::mouseMove(const juce::MouseEvent& e)
{
    if (displayArea.contains(e.position))
    {
        isHovering = true;
        hoverPosition = e.position;
        repaint();
    }
    else
    {
        isHovering = false;
        repaint();
    }
}

void SpectrumDisplay::mouseExit(const juce::MouseEvent&)
{
    isHovering = false;
    repaint();
}

//==============================================================================
float SpectrumDisplay::getFrequencyAtX(float x) const
{
    float normalizedX = (x - displayArea.getX()) / displayArea.getWidth();
    normalizedX = juce::jlimit(0.0f, 1.0f, normalizedX);

    float logMinF = std::log10(minFrequency);
    float logMaxF = std::log10(maxFrequency);
    float logFreq = logMinF + normalizedX * (logMaxF - logMinF);

    return std::pow(10.0f, logFreq);
}

float SpectrumDisplay::getXForFrequency(float freq) const
{
    freq = juce::jlimit(minFrequency, maxFrequency, freq);

    float logMinF = std::log10(minFrequency);
    float logMaxF = std::log10(maxFrequency);
    float logFreq = std::log10(freq);

    float normalizedX = (logFreq - logMinF) / (logMaxF - logMinF);
    return displayArea.getX() + normalizedX * displayArea.getWidth();
}

float SpectrumDisplay::getYForDB(float dB) const
{
    float normalizedY = (dB - maxDisplayDB) / (minDisplayDB - maxDisplayDB);
    normalizedY = juce::jlimit(0.0f, 1.0f, normalizedY);
    return displayArea.getY() + normalizedY * displayArea.getHeight();
}

float SpectrumDisplay::getDBAtY(float y) const
{
    float normalizedY = (y - displayArea.getY()) / displayArea.getHeight();
    normalizedY = juce::jlimit(0.0f, 1.0f, normalizedY);
    return maxDisplayDB + normalizedY * (minDisplayDB - maxDisplayDB);
}

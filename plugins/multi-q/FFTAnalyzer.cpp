#include "FFTAnalyzer.h"

//==============================================================================
FFTAnalyzer::FFTAnalyzer()
{
    std::fill(currentMagnitudes.begin(), currentMagnitudes.end(), -100.0f);
    std::fill(peakHoldMagnitudes.begin(), peakHoldMagnitudes.end(), -100.0f);
}

//==============================================================================
void FFTAnalyzer::paint(juce::Graphics& g)
{
    if (!analyzerEnabled)
        return;

    auto bounds = getLocalBounds().toFloat();

    // Create the magnitude path
    auto path = createMagnitudePath();

    if (!path.isEmpty())
    {
        // Get path bounds for gradient
        auto pathBounds = path.getBounds();

        // Create gradient fill from spectrum color at top to transparent at bottom (~25% opacity max)
        juce::ColourGradient fillGradient(
            fillColor.withAlpha(0.28f), 0, pathBounds.getY(),
            fillColor.withAlpha(0.01f), 0, bounds.getBottom(),
            false);
        fillGradient.addColour(0.3, fillColor.withAlpha(0.18f));
        fillGradient.addColour(0.6, fillColor.withAlpha(0.08f));

        g.setGradientFill(fillGradient);
        g.fillPath(path);

        // Soft outer glow for the spectrum line (very subtle)
        g.setColour(lineColor.withAlpha(0.1f));
        g.strokePath(path, juce::PathStrokeType(3.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Inner glow
        g.setColour(lineColor.withAlpha(0.2f));
        g.strokePath(path, juce::PathStrokeType(1.5f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Main spectrum line
        g.setColour(lineColor.withAlpha(0.5f));
        g.strokePath(path, juce::PathStrokeType(0.75f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Draw peak hold line if enabled (thin line above spectrum)
    if (showPeakHold)
    {
        juce::Path peakPath;
        bool pathStarted = false;

        for (int i = 0; i < 2048; ++i)
        {
            // Map bin to frequency (logarithmic)
            float normalizedBin = static_cast<float>(i) / 2047.0f;
            float freq = minFrequency * std::pow(maxFrequency / minFrequency, normalizedBin);

            float x = getXForFrequency(freq);
            float y = getYForDB(peakHoldMagnitudes[static_cast<size_t>(i)]);

            if (!pathStarted)
            {
                peakPath.startNewSubPath(x, y);
                pathStarted = true;
            }
            else
            {
                peakPath.lineTo(x, y);
            }
        }

        // Peak hold with subtle glow
        g.setColour(juce::Colour(0x20ffff66));
        g.strokePath(peakPath, juce::PathStrokeType(3.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0x60ffff66));
        g.strokePath(peakPath, juce::PathStrokeType(1.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
void FFTAnalyzer::updateMagnitudes(const std::array<float, 2048>& magnitudes)
{
    currentMagnitudes = magnitudes;

    // Update peak hold
    for (size_t i = 0; i < 2048; ++i)
    {
        if (magnitudes[i] > peakHoldMagnitudes[i])
            peakHoldMagnitudes[i] = magnitudes[i];
    }

    repaint();
}

//==============================================================================
void FFTAnalyzer::setDisplayRange(float minDB, float maxDB)
{
    minDisplayDB = minDB;
    maxDisplayDB = maxDB;
    repaint();
}

void FFTAnalyzer::setFrequencyRange(float minHz, float maxHz)
{
    minFrequency = minHz;
    maxFrequency = maxHz;
    repaint();
}

//==============================================================================
float FFTAnalyzer::getFrequencyAtX(float x) const
{
    auto bounds = getLocalBounds().toFloat();
    float normalized = (x - bounds.getX()) / bounds.getWidth();
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    // Logarithmic mapping
    return minFrequency * std::pow(maxFrequency / minFrequency, normalized);
}

float FFTAnalyzer::getXForFrequency(float freq) const
{
    auto bounds = getLocalBounds().toFloat();

    if (minFrequency <= 0.0f || maxFrequency <= minFrequency)
        return bounds.getCentreX();

    // Logarithmic mapping
    float normalized = std::log(freq / minFrequency) / std::log(maxFrequency / minFrequency);
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    return bounds.getX() + normalized * bounds.getWidth();
}
float FFTAnalyzer::getYForDB(float dB) const
{
    auto bounds = getLocalBounds().toFloat();

    float range = maxDisplayDB - minDisplayDB;
    if (range == 0.0f)
        return bounds.getCentreY();

    // Linear mapping in dB space
    float normalized = (dB - minDisplayDB) / range;
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    // Invert for screen coordinates (top = max, bottom = min)
    return bounds.getBottom() - normalized * bounds.getHeight();
}
float FFTAnalyzer::getDBAtY(float y) const
{
    auto bounds = getLocalBounds().toFloat();

    float normalized = (bounds.getBottom() - y) / bounds.getHeight();
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    return minDisplayDB + normalized * (maxDisplayDB - minDisplayDB);
}

//==============================================================================
juce::Path FFTAnalyzer::createMagnitudePath() const
{
    juce::Path path;
    auto bounds = getLocalBounds().toFloat();

    if (bounds.isEmpty())
        return path;

    bool pathStarted = false;

    // We'll iterate through display x positions and find corresponding bins
    // The magnitudes array is already mapped to logarithmic frequency bins by the processor
    int numPoints = static_cast<int>(bounds.getWidth());

    for (int px = 0; px < numPoints; ++px)
    {
        float x = bounds.getX() + static_cast<float>(px);
        float freq = getFrequencyAtX(x);

        // Find the corresponding bin (the magnitudes array is already frequency-mapped)
        // Assume magnitudes are linearly mapped 0-2048 covering log(20)-log(20000)
        float normalizedFreq = std::log(freq / minFrequency) / std::log(maxFrequency / minFrequency);
        int bin = static_cast<int>(normalizedFreq * 2047.0f);
        bin = juce::jlimit(0, 2047, bin);

        float dB = currentMagnitudes[static_cast<size_t>(bin)];
        float y = getYForDB(dB);

        if (!pathStarted)
        {
            path.startNewSubPath(x, y);
            pathStarted = true;
        }
        else
        {
            path.lineTo(x, y);
        }
    }

    // Close path for fill
    if (pathStarted)
    {
        path.lineTo(bounds.getRight(), bounds.getBottom());
        path.lineTo(bounds.getX(), bounds.getBottom());
        path.closeSubPath();
    }

    return path;
}

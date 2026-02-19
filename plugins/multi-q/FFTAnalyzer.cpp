#include "FFTAnalyzer.h"
#include <vector>

//==============================================================================
FFTAnalyzer::FFTAnalyzer()
{
    std::fill(currentMagnitudes.begin(), currentMagnitudes.end(), -100.0f);
    std::fill(smoothedMagnitudes.begin(), smoothedMagnitudes.end(), -100.0f);
    std::fill(peakHoldMagnitudes.begin(), peakHoldMagnitudes.end(), -100.0f);
    std::fill(frozenMagnitudes.begin(), frozenMagnitudes.end(), -100.0f);
    std::fill(preSmoothedMagnitudes.begin(), preSmoothedMagnitudes.end(), -100.0f);
}

//==============================================================================
float FFTAnalyzer::getTemporalSmoothingCoeff() const
{
    // Higher coefficient = more smoothing (slower response)
    // Coefficient represents how much of the previous value to keep
    switch (smoothingMode)
    {
        case SmoothingMode::Off:    return 0.0f;   // No smoothing
        case SmoothingMode::Light:  return 0.7f;   // Fast response
        case SmoothingMode::Medium: return 0.85f;  // Balanced
        case SmoothingMode::Heavy:  return 0.93f;  // Very smooth
        default:                    return 0.85f;
    }
}

int FFTAnalyzer::getSpatialSmoothingWidth() const
{
    // Width of the spatial smoothing kernel (number of bins to average)
    switch (smoothingMode)
    {
        case SmoothingMode::Off:    return 0;
        case SmoothingMode::Light:  return 1;   // ±1 bin (3-bin average)
        case SmoothingMode::Medium: return 2;   // ±2 bins (5-bin average)
        case SmoothingMode::Heavy:  return 4;   // ±4 bins (9-bin average)
        default:                    return 2;
    }
}

void FFTAnalyzer::applySpatialSmoothing(std::array<float, 2048>& magnitudes) const
{
    int width = getSpatialSmoothingWidth();
    if (width == 0)
        return;

    // Create a temporary copy for the averaging
    std::array<float, 2048> temp = magnitudes;

    for (int i = 0; i < 2048; ++i)
    {
        float sum = 0.0f;
        float totalWeight = 0.0f;

        // Average over the window
        for (int j = -width; j <= width; ++j)
        {
            int idx = i + j;
            if (idx >= 0 && idx < 2048)
            {
                // Weight center bins more heavily (triangular window)
                float weight = 1.0f - std::abs(static_cast<float>(j)) / static_cast<float>(width + 1);
                sum += temp[static_cast<size_t>(idx)] * weight;
                totalWeight += weight;
            }
        }

        if (totalWeight > 0.0f)
            magnitudes[static_cast<size_t>(i)] = sum / totalWeight;
    }
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

    // Draw pre-EQ reference spectrum (behind main spectrum, muted colors)
    if (showPreSpectrum)
    {
        auto prePath = createMagnitudePath(preSmoothedMagnitudes);

        if (!prePath.isEmpty())
        {
            auto prePathBounds = prePath.getBounds();

            juce::ColourGradient preFillGradient(
                preFillColor.withAlpha(0.18f), 0, prePathBounds.getY(),
                preFillColor.withAlpha(0.01f), 0, bounds.getBottom(),
                false);
            preFillGradient.addColour(0.3, preFillColor.withAlpha(0.10f));
            preFillGradient.addColour(0.6, preFillColor.withAlpha(0.04f));

            g.setGradientFill(preFillGradient);
            g.fillPath(prePath);

            g.setColour(preLineColor.withAlpha(0.15f));
            g.strokePath(prePath, juce::PathStrokeType(2.0f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(preLineColor.withAlpha(0.35f));
            g.strokePath(prePath, juce::PathStrokeType(0.75f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
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

    // Draw frozen spectrum reference line (cyan color to distinguish from live spectrum)
    if (spectrumFrozen)
    {
        juce::Path frozenPath;
        bool pathStarted = false;

        for (int i = 0; i < 2048; ++i)
        {
            // Map bin to frequency (logarithmic)
            float normalizedBin = static_cast<float>(i) / 2047.0f;
            float freq = minFrequency * std::pow(maxFrequency / minFrequency, normalizedBin);

            float x = getXForFrequency(freq);
            float y = getYForDB(frozenMagnitudes[static_cast<size_t>(i)]);

            if (!pathStarted)
            {
                frozenPath.startNewSubPath(x, y);
                pathStarted = true;
            }
            else
            {
                frozenPath.lineTo(x, y);
            }
        }

        // Frozen spectrum with cyan glow for visibility
        g.setColour(juce::Colour(0x2066ccff));  // Outer glow
        g.strokePath(frozenPath, juce::PathStrokeType(4.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0x6066ccff));  // Main line
        g.strokePath(frozenPath, juce::PathStrokeType(1.5f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
void FFTAnalyzer::updateMagnitudes(const std::array<float, 2048>& magnitudes)
{
    currentMagnitudes = magnitudes;

    // Apply temporal smoothing (exponential moving average)
    float smoothCoeff = getTemporalSmoothingCoeff();
    if (smoothCoeff > 0.0f)
    {
        for (size_t i = 0; i < 2048; ++i)
        {
            // Smooth attack (rising) and release (falling) separately
            // Attack is faster than release for better transient visibility
            float attackCoeff = smoothCoeff * 0.5f;   // Faster attack
            float releaseCoeff = smoothCoeff;          // Normal release

            if (magnitudes[i] > smoothedMagnitudes[i])
            {
                // Rising (attack) - use faster coefficient
                smoothedMagnitudes[i] = attackCoeff * smoothedMagnitudes[i] +
                                        (1.0f - attackCoeff) * magnitudes[i];
            }
            else
            {
                // Falling (release) - use normal coefficient
                smoothedMagnitudes[i] = releaseCoeff * smoothedMagnitudes[i] +
                                        (1.0f - releaseCoeff) * magnitudes[i];
            }
        }

        // Apply spatial smoothing to the temporally smoothed data
        applySpatialSmoothing(smoothedMagnitudes);
    }
    else
    {
        // No smoothing - use raw magnitudes
        smoothedMagnitudes = magnitudes;
    }

    // Update peak hold (uses raw magnitudes for accurate peak detection)
    for (size_t i = 0; i < 2048; ++i)
    {
        if (magnitudes[i] > peakHoldMagnitudes[i])
            peakHoldMagnitudes[i] = magnitudes[i];
    }

    repaint();
}

void FFTAnalyzer::updatePreMagnitudes(const std::array<float, 2048>& magnitudes)
{
    float smoothCoeff = getTemporalSmoothingCoeff();
    if (smoothCoeff > 0.0f)
    {
        for (size_t i = 0; i < 2048; ++i)
        {
            float attackCoeff = smoothCoeff * 0.5f;
            float releaseCoeff = smoothCoeff;

            if (magnitudes[i] > preSmoothedMagnitudes[i])
                preSmoothedMagnitudes[i] = attackCoeff * preSmoothedMagnitudes[i] +
                                           (1.0f - attackCoeff) * magnitudes[i];
            else
                preSmoothedMagnitudes[i] = releaseCoeff * preSmoothedMagnitudes[i] +
                                           (1.0f - releaseCoeff) * magnitudes[i];
        }
        applySpatialSmoothing(preSmoothedMagnitudes);
    }
    else
    {
        preSmoothedMagnitudes = magnitudes;
    }

    if (showPreSpectrum)
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
    return createMagnitudePath(smoothedMagnitudes);
}

juce::Path FFTAnalyzer::createMagnitudePath(const std::array<float, 2048>& mags) const
{
    juce::Path path;
    auto bounds = getLocalBounds().toFloat();

    if (bounds.isEmpty())
        return path;

    bool pathStarted = false;

    int numPoints = static_cast<int>(bounds.getWidth());

    std::vector<juce::Point<float>> points;
    points.reserve(static_cast<size_t>(numPoints));

    for (int px = 0; px < numPoints; ++px)
    {
        float x = bounds.getX() + static_cast<float>(px);
        float freq = getFrequencyAtX(x);

        float normalizedFreq = std::log(freq / minFrequency) / std::log(maxFrequency / minFrequency);
        int bin = static_cast<int>(normalizedFreq * 2047.0f);
        bin = juce::jlimit(0, 2047, bin);

        float dB = mags[static_cast<size_t>(bin)];
        float y = getYForDB(dB);

        points.emplace_back(x, y);
    }

    if (points.empty())
        return path;

    path.startNewSubPath(points[0]);
    pathStarted = true;

    for (size_t i = 1; i < points.size(); ++i)
        path.lineTo(points[i]);

    if (pathStarted)
    {
        path.lineTo(bounds.getRight(), bounds.getBottom());
        path.lineTo(bounds.getX(), bounds.getBottom());
        path.closeSubPath();
    }

    return path;
}

#include "FFTAnalyzer.h"
#include <vector>

//==============================================================================
FFTAnalyzer::FFTAnalyzer()
{
    setOpaque(false);  // Ensure parent paints behind us when we draw nothing

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
    switch (smoothingMode.load(std::memory_order_relaxed))
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
    switch (smoothingMode.load(std::memory_order_relaxed))
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
    if (!analyzerEnabled.load(std::memory_order_relaxed))
        return;

    // Copy shared arrays under a short-lived lock to minimize audio-thread contention
    std::array<float, 2048> localSmoothed;
    std::array<float, 2048> localPreSmoothed;
    std::array<float, 2048> localFrozen;
    std::array<float, 2048> localPeakHold;
    bool localShowPreSpectrum;
    bool localShowPeakHold;
    bool localSpectrumFrozen;
    {
        juce::SpinLock::ScopedLockType lock(magnitudeLock);
        localSmoothed = smoothedMagnitudes;
        localPreSmoothed = preSmoothedMagnitudes;
        localFrozen = frozenMagnitudes;
        localPeakHold = peakHoldMagnitudes;
        localShowPreSpectrum = showPreSpectrum.load(std::memory_order_relaxed);
        localShowPeakHold = showPeakHold.load(std::memory_order_relaxed);
        localSpectrumFrozen = spectrumFrozen.load(std::memory_order_relaxed);
    }

    auto bounds = getLocalBounds().toFloat();

    // Load colors atomically for thread-safe access
    juce::Colour fillColor = juce::Colour(fillColorARGB.load(std::memory_order_relaxed));
    juce::Colour lineColor = juce::Colour(lineColorARGB.load(std::memory_order_relaxed));
    juce::Colour preFillColor = juce::Colour(preFillColorARGB.load(std::memory_order_relaxed));
    juce::Colour preLineColor = juce::Colour(preLineColorARGB.load(std::memory_order_relaxed));

    // Draw pre-EQ reference spectrum first (behind main spectrum, muted colors)
    if (localShowPreSpectrum)
    {
        auto prePath = createMagnitudePath(localPreSmoothed);

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

    // Create the magnitude path
    auto path = createMagnitudePath(localSmoothed);

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
    if (localShowPeakHold)
    {
        juce::Path peakPath;
        bool pathStarted = false;

        for (int i = 0; i < 2048; ++i)
        {
            // Map bin to frequency (logarithmic)
            float normalizedBin = static_cast<float>(i) / 2047.0f;
            float freq = minFrequency * std::pow(maxFrequency / minFrequency, normalizedBin);

            float x = getXForFrequency(freq);
            float y = getYForDB(localPeakHold[static_cast<size_t>(i)]);

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
    if (localSpectrumFrozen)
    {
        juce::Path frozenPath;
        bool pathStarted = false;

        for (int i = 0; i < 2048; ++i)
        {
            // Map bin to frequency (logarithmic)
            float normalizedBin = static_cast<float>(i) / 2047.0f;
            float freq = minFrequency * std::pow(maxFrequency / minFrequency, normalizedBin);

            float x = getXForFrequency(freq);
            float y = getYForDB(localFrozen[static_cast<size_t>(i)]);

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
    std::array<float, 2048> tempSmoothed;
    std::array<float, 2048> tempPeakHold;
    std::array<float, 2048> prevSmoothed;
    float smoothCoeff = getTemporalSmoothingCoeff();

    // Brief lock to read previous state and store raw magnitudes
    {
        juce::SpinLock::ScopedLockType lock(magnitudeLock);
        currentMagnitudes = magnitudes;
        prevSmoothed = smoothedMagnitudes;
        tempPeakHold = peakHoldMagnitudes;
    }

    // Temporal smoothing computed outside the lock
    if (smoothCoeff > 0.0f)
    {
        float attackCoeff = smoothCoeff * 0.5f;
        float releaseCoeff = smoothCoeff;

        for (size_t i = 0; i < 2048; ++i)
        {
            if (magnitudes[i] > prevSmoothed[i])
                tempSmoothed[i] = attackCoeff * prevSmoothed[i] +
                                  (1.0f - attackCoeff) * magnitudes[i];
            else
                tempSmoothed[i] = releaseCoeff * prevSmoothed[i] +
                                  (1.0f - releaseCoeff) * magnitudes[i];
        }
    }
    else
    {
        tempSmoothed = magnitudes;
    }

    // Peak hold with time-based decay (outside the lock)
    double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    double prevTime = lastPeakDecayTime.load(std::memory_order_acquire);
    float deltaTime = (prevTime > 0.0)
        ? static_cast<float>(now - prevTime)
        : 0.0f;
    deltaTime = juce::jlimit(0.0f, 0.5f, deltaTime);  // Clamp to avoid large jumps
    lastPeakDecayTime.store(now, std::memory_order_release);

    float decayAmount = peakDecayRateDbPerSec * deltaTime;
    for (size_t i = 0; i < 2048; ++i)
    {
        tempPeakHold[i] -= decayAmount;
        if (tempPeakHold[i] < -100.0f)
            tempPeakHold[i] = -100.0f;
        if (magnitudes[i] > tempPeakHold[i])
            tempPeakHold[i] = magnitudes[i];
    }

    // Apply spatial smoothing outside the lock (no shared state access)
    if (smoothCoeff > 0.0f)
        applySpatialSmoothing(tempSmoothed);

    // Brief lock to publish results
    {
        juce::SpinLock::ScopedLockType lock(magnitudeLock);
        smoothedMagnitudes = tempSmoothed;
        peakHoldMagnitudes = tempPeakHold;
    }

    repaint();
}

void FFTAnalyzer::updatePreMagnitudes(const std::array<float, 2048>& magnitudes)
{
    std::array<float, 2048> tempPreSmoothed;
    std::array<float, 2048> prevPreSmoothed;
    float smoothCoeff = getTemporalSmoothingCoeff();

    // Brief lock to read previous state
    {
        juce::SpinLock::ScopedLockType lock(magnitudeLock);
        prevPreSmoothed = preSmoothedMagnitudes;
    }

    // Temporal smoothing computed outside the lock
    if (smoothCoeff > 0.0f)
    {
        float attackCoeff = smoothCoeff * 0.5f;
        float releaseCoeff = smoothCoeff;

        for (size_t i = 0; i < 2048; ++i)
        {
            if (magnitudes[i] > prevPreSmoothed[i])
                tempPreSmoothed[i] = attackCoeff * prevPreSmoothed[i] +
                                     (1.0f - attackCoeff) * magnitudes[i];
            else
                tempPreSmoothed[i] = releaseCoeff * prevPreSmoothed[i] +
                                     (1.0f - releaseCoeff) * magnitudes[i];
        }

        applySpatialSmoothing(tempPreSmoothed);
    }
    else
    {
        tempPreSmoothed = magnitudes;
    }

    // Brief lock to publish results
    {
        juce::SpinLock::ScopedLockType lock(magnitudeLock);
        preSmoothedMagnitudes = tempPreSmoothed;
    }

    if (showPreSpectrum.load(std::memory_order_relaxed))
        repaint();
}

//==============================================================================
void FFTAnalyzer::toggleFreeze()
{
    {
        juce::SpinLock::ScopedLockType lock(magnitudeLock);
        if (spectrumFrozen.load(std::memory_order_relaxed))
        {
            spectrumFrozen.store(false, std::memory_order_relaxed);
        }
        else
        {
            frozenMagnitudes = smoothedMagnitudes;
            spectrumFrozen.store(true, std::memory_order_relaxed);
        }
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
    std::array<float, 2048> localMags;
    {
        juce::SpinLock::ScopedLockType lock(magnitudeLock);
        localMags = smoothedMagnitudes;
    }
    return createMagnitudePath(localMags);
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

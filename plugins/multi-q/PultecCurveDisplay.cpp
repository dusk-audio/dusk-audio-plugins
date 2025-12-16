#include "PultecCurveDisplay.h"
#include "EQBand.h"
#include <cmath>

PultecCurveDisplay::PultecCurveDisplay(MultiQ& processor)
    : audioProcessor(processor)
{
    setOpaque(true);
    startTimerHz(30);  // Update at 30fps

    // Force initial parameter read
    timerCallback();
}

PultecCurveDisplay::~PultecCurveDisplay()
{
    stopTimer();
}

void PultecCurveDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Early exit if bounds are invalid
    if (bounds.getWidth() < 10 || bounds.getHeight() < 10)
        return;

    // Define drawing area with margins for labels
    const float leftMargin = 30.0f;   // Space for dB labels
    const float bottomMargin = 18.0f; // Space for frequency labels
    const float topMargin = 6.0f;
    const float rightMargin = 6.0f;

    auto graphArea = bounds;
    graphArea.removeFromLeft(leftMargin);
    graphArea.removeFromBottom(bottomMargin);
    graphArea.removeFromTop(topMargin);
    graphArea.removeFromRight(rightMargin);

    // Vintage background with subtle gradient
    g.setColour(juce::Colour(backgroundColor));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Inner darker area for the graph with warm tint
    g.setColour(juce::Colour(0xff181410));
    g.fillRoundedRectangle(graphArea.reduced(1), 3.0f);

    // Subtle vignette effect for vintage look
    juce::ColourGradient vignette(juce::Colour(0x00000000), graphArea.getCentreX(), graphArea.getCentreY(),
                                   juce::Colour(0x30000000), graphArea.getX(), graphArea.getY(), true);
    g.setGradientFill(vignette);
    g.fillRoundedRectangle(graphArea, 3.0f);

    // Border with vintage brass look
    g.setColour(juce::Colour(0xff504030));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    // Draw vintage-style grid within graph area
    drawVintageGrid(g, graphArea);

    // Save graphics state before clipping
    g.saveState();

    // Clip to graph area for curves
    g.reduceClipRegion(graphArea.reduced(2).toNearestInt());

    // Draw individual band curves (subtle, behind combined)
    // LF Boost curve (warm brown)
    if (cachedParams.lfBoostGain > 0.1f)
    {
        drawBandCurve(g, graphArea, juce::Colour(lfBoostColor).withAlpha(0.5f),
                      [this](float f) { return calculateLFBoostResponse(f); });
    }

    // LF Atten curve (darker brown) - always show if active
    if (cachedParams.lfAttenGain > 0.1f)
    {
        drawBandCurve(g, graphArea, juce::Colour(lfAttenColor).withAlpha(0.5f),
                      [this](float f) { return calculateLFAttenResponse(f); });
    }

    // HF Boost curve (gold)
    if (cachedParams.hfBoostGain > 0.1f)
    {
        drawBandCurve(g, graphArea, juce::Colour(hfBoostColor).withAlpha(0.5f),
                      [this](float f) { return calculateHFBoostResponse(f); });
    }

    // HF Atten curve (muted gold)
    if (cachedParams.hfAttenGain > 0.1f)
    {
        drawBandCurve(g, graphArea, juce::Colour(hfAttenColor).withAlpha(0.5f),
                      [this](float f) { return calculateHFAttenResponse(f); });
    }

    // Always draw combined curve
    drawCombinedCurve(g, graphArea);

    // Restore graphics state
    g.restoreState();
}

void PultecCurveDisplay::resized()
{
    needsRepaint = true;
    repaint();  // Force immediate repaint when bounds change
}

void PultecCurveDisplay::timerCallback()
{
    // Check if parameters have changed - use Pultec mode parameter IDs
    auto& params = audioProcessor.parameters;

    CachedParams newParams;

    // Read Pultec mode parameters
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecLfBoostGain))
        newParams.lfBoostGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecLfBoostFreq))
        newParams.lfBoostFreq = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecLfAttenGain))
        newParams.lfAttenGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfBoostGain))
        newParams.hfBoostGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfBoostFreq))
        newParams.hfBoostFreq = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfBoostBandwidth))
        newParams.hfBoostBandwidth = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfAttenGain))
        newParams.hfAttenGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfAttenFreq))
        newParams.hfAttenFreq = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecTubeDrive))
        newParams.tubeDrive = p->load();

    // Compare using epsilon for floating point values
    auto floatsDiffer = [](float a, float b) {
        return std::abs(a - b) > 0.001f;
    };

    bool changed = floatsDiffer(newParams.lfBoostGain, cachedParams.lfBoostGain) ||
                   floatsDiffer(newParams.lfBoostFreq, cachedParams.lfBoostFreq) ||
                   floatsDiffer(newParams.lfAttenGain, cachedParams.lfAttenGain) ||
                   floatsDiffer(newParams.hfBoostGain, cachedParams.hfBoostGain) ||
                   floatsDiffer(newParams.hfBoostFreq, cachedParams.hfBoostFreq) ||
                   floatsDiffer(newParams.hfBoostBandwidth, cachedParams.hfBoostBandwidth) ||
                   floatsDiffer(newParams.hfAttenGain, cachedParams.hfAttenGain) ||
                   floatsDiffer(newParams.hfAttenFreq, cachedParams.hfAttenFreq) ||
                   floatsDiffer(newParams.tubeDrive, cachedParams.tubeDrive);

    if (changed || needsRepaint)
    {
        cachedParams = newParams;
        needsRepaint = false;
        repaint();
    }
}

float PultecCurveDisplay::freqToX(float freq, const juce::Rectangle<float>& area) const
{
    // Logarithmic frequency to X position within graph area
    float logMin = std::log10(minFreq);
    float logMax = std::log10(maxFreq);
    float logFreq = std::log10(std::max(freq, minFreq));

    float normalized = (logFreq - logMin) / (logMax - logMin);
    return area.getX() + area.getWidth() * normalized;
}

float PultecCurveDisplay::xToFreq(float x, const juce::Rectangle<float>& area) const
{
    float logMin = std::log10(minFreq);
    float logMax = std::log10(maxFreq);
    float normalized = (x - area.getX()) / area.getWidth();
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    return std::pow(10.0f, logMin + normalized * (logMax - logMin));
}

float PultecCurveDisplay::dbToY(float db, const juce::Rectangle<float>& area) const
{
    // dB to Y position (inverted - higher dB = lower Y)
    float normalized = (db - minDB) / (maxDB - minDB);
    return area.getBottom() - area.getHeight() * normalized;
}

void PultecCurveDisplay::drawVintageGrid(juce::Graphics& g, const juce::Rectangle<float>& area)
{
    // Vertical grid lines at key frequencies (Pultec-relevant)
    // Emphasize the Pultec frequency selections
    const float freqLines[] = { 30.0f, 60.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                 3000.0f, 5000.0f, 8000.0f, 10000.0f, 16000.0f };

    for (float freq : freqLines)
    {
        float x = freqToX(freq, area);
        // Highlight Pultec switch positions
        bool isPultecFreq = (freq == 30.0f || freq == 60.0f || freq == 100.0f ||
                            freq == 3000.0f || freq == 5000.0f || freq == 8000.0f ||
                            freq == 10000.0f || freq == 16000.0f);

        g.setColour(juce::Colour(isPultecFreq ? 0xff3d3830 : 0xff2a2620));
        g.drawLine(x, area.getY(), x, area.getBottom(), isPultecFreq ? 1.0f : 0.5f);
    }

    // Horizontal grid lines at key dB levels
    const float dbLines[] = { -20.0f, -10.0f, 0.0f, 10.0f, 20.0f };

    for (float db : dbLines)
    {
        float y = dbToY(db, area);
        bool isZero = (std::abs(db) < 0.1f);

        g.setColour(juce::Colour(isZero ? 0xff504840 : 0xff2d2a26));
        g.drawLine(area.getX(), y, area.getRight(), y, isZero ? 1.5f : 0.5f);
    }

    // Frequency labels at bottom with vintage styling
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.setColour(juce::Colour(0xff807060));  // Warm brown text

    auto drawFreqLabel = [&](float freq, const juce::String& text) {
        float x = freqToX(freq, area);
        g.drawText(text, static_cast<int>(x) - 18, static_cast<int>(area.getBottom()) + 3, 36, 14,
                   juce::Justification::centred);
    };

    drawFreqLabel(60.0f, "60");
    drawFreqLabel(100.0f, "100");
    drawFreqLabel(1000.0f, "1k");
    drawFreqLabel(5000.0f, "5k");
    drawFreqLabel(10000.0f, "10k");

    // dB labels on left with vintage styling
    auto drawDbLabel = [&](float db, const juce::String& text) {
        float y = dbToY(db, area);
        g.drawText(text, 4, static_cast<int>(y) - 7, 24, 14, juce::Justification::right);
    };

    drawDbLabel(20.0f, "+20");
    drawDbLabel(0.0f, "0");
    drawDbLabel(-20.0f, "-20");
}

void PultecCurveDisplay::drawBandCurve(juce::Graphics& g, const juce::Rectangle<float>& area,
                                        juce::Colour color, std::function<float(float)> getMagnitude)
{
    juce::Path path;
    bool pathStarted = false;

    const int numPoints = static_cast<int>(area.getWidth());
    for (int i = 0; i <= numPoints; ++i)
    {
        float x = area.getX() + static_cast<float>(i);
        float freq = xToFreq(x, area);
        float db = getMagnitude(freq);

        // Clamp dB to visible range
        db = juce::jlimit(minDB, maxDB, db);

        float y = dbToY(db, area);

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

    g.setColour(color);
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

void PultecCurveDisplay::drawCombinedCurve(juce::Graphics& g, const juce::Rectangle<float>& area)
{
    juce::Path path;
    juce::Path fillPath;
    bool pathStarted = false;

    const int numPoints = static_cast<int>(area.getWidth());
    float zeroY = dbToY(0.0f, area);

    for (int i = 0; i <= numPoints; ++i)
    {
        float x = area.getX() + static_cast<float>(i);
        float freq = xToFreq(x, area);
        float db = calculateCombinedResponse(freq);

        // Clamp dB to visible range
        db = juce::jlimit(minDB, maxDB, db);

        float y = dbToY(db, area);

        if (!pathStarted)
        {
            path.startNewSubPath(x, y);
            fillPath.startNewSubPath(x, zeroY);
            fillPath.lineTo(x, y);
            pathStarted = true;
        }
        else
        {
            path.lineTo(x, y);
            fillPath.lineTo(x, y);
        }
    }

    // Close fill path
    fillPath.lineTo(area.getRight(), zeroY);
    fillPath.closeSubPath();

    // Draw vintage-style fill with warm tint
    g.setColour(juce::Colour(combinedColor).withAlpha(0.1f));
    g.fillPath(fillPath);

    // Draw warm glow effect (multiple passes)
    g.setColour(juce::Colour(combinedColor).withAlpha(0.06f));
    g.strokePath(path, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(juce::Colour(combinedColor).withAlpha(0.12f));
    g.strokePath(path, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Draw main line (cream/ivory color)
    g.setColour(juce::Colour(combinedColor));
    g.strokePath(path, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Filter response calculations matching Pultec characteristics

float PultecCurveDisplay::calculateLFBoostResponse(float freq) const
{
    if (cachedParams.lfBoostGain < 0.1f)
        return 0.0f;

    float fc = cachedParams.lfBoostFreq;
    float gain = cachedParams.lfBoostGain * 1.4f;  // 0-10 maps to ~0-14 dB

    // Pultec LF boost is a very broad resonant peak
    // Using a broad Gaussian-like response
    float logRatio = std::log(freq / fc);
    float bandwidth = 2.0f;  // Very broad Q

    return gain * std::exp(-0.5f * std::pow(logRatio / bandwidth, 2.0f));
}

float PultecCurveDisplay::calculateLFAttenResponse(float freq) const
{
    if (cachedParams.lfAttenGain < 0.1f)
        return 0.0f;

    float fc = cachedParams.lfBoostFreq;  // LF atten uses same freq as boost
    float gain = -cachedParams.lfAttenGain * 1.6f;  // 0-10 maps to ~0-16 dB cut

    // LF attenuation is a shelf - smoother S-curve
    float logRatio = std::log10(freq / fc);

    // Sigmoid-like transition
    float transitionWidth = 0.6f;
    float normalized = 0.5f * (1.0f + std::tanh(-logRatio / transitionWidth));

    return gain * normalized;
}

float PultecCurveDisplay::calculateHFBoostResponse(float freq) const
{
    if (cachedParams.hfBoostGain < 0.1f)
        return 0.0f;

    float fc = cachedParams.hfBoostFreq;
    float gain = cachedParams.hfBoostGain * 1.6f;  // 0-10 maps to ~0-16 dB

    // Bandwidth control: Sharp (high Q) to Broad (low Q)
    // bandwidth 0 = sharp (narrow), 1 = broad (wide)
    float q = juce::jmap(cachedParams.hfBoostBandwidth, 0.0f, 1.0f, 2.5f, 0.5f);
    float bandwidth = 1.0f / q;

    // Peak filter response
    float logRatio = std::log(freq / fc);
    return gain * std::exp(-0.5f * std::pow(logRatio / (bandwidth * 0.6f), 2.0f));
}

float PultecCurveDisplay::calculateHFAttenResponse(float freq) const
{
    if (cachedParams.hfAttenGain < 0.1f)
        return 0.0f;

    float fc = cachedParams.hfAttenFreq;
    float gain = -cachedParams.hfAttenGain * 2.0f;  // 0-10 maps to ~0-20 dB cut

    // High shelf attenuation
    float logRatio = std::log10(freq / fc);

    // Sigmoid-like transition for shelf
    float transitionWidth = 0.5f;
    float normalized = 0.5f * (1.0f + std::tanh(logRatio / transitionWidth));

    return gain * normalized;
}

float PultecCurveDisplay::calculateCombinedResponse(float freq) const
{
    float response = 0.0f;

    // Add all band responses
    // The Pultec "trick" - boost and cut at same frequency interact
    response += calculateLFBoostResponse(freq);
    response += calculateLFAttenResponse(freq);
    response += calculateHFBoostResponse(freq);
    response += calculateHFAttenResponse(freq);

    return response;
}

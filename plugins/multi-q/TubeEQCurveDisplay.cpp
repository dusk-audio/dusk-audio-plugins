#include "TubeEQCurveDisplay.h"
#include "EQBand.h"
#include <cmath>

TubeEQCurveDisplay::TubeEQCurveDisplay(MultiQ& processor)
    : audioProcessor(processor)
{
    setOpaque(true);

    // Create FFT analyzer component (displayed behind EQ curves)
    analyzer = std::make_unique<FFTAnalyzer>();
    addAndMakeVisible(analyzer.get());
    analyzer->setFillColor(juce::Colour(0x30888888));
    analyzer->setLineColor(juce::Colour(0x80AAAAAA));

    startTimerHz(30);  // Update at 30fps

    // Force initial parameter read
    timerCallback();
}

TubeEQCurveDisplay::~TubeEQCurveDisplay()
{
    stopTimer();
}

void TubeEQCurveDisplay::paint(juce::Graphics& g)
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

    // Dark professional background
    g.setColour(juce::Colour(backgroundColor));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Inner darker area for the graph
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRoundedRectangle(graphArea.reduced(1), 3.0f);

    // Border with subtle highlight
    g.setColour(juce::Colour(0xff505050));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    // Draw vintage-style grid within graph area
    drawVintageGrid(g, graphArea);

    // Save graphics state before clipping
    g.saveState();

    // Clip to graph area for curves
    g.reduceClipRegion(graphArea.reduced(2).toNearestInt());

    // Draw individual band curves (subtle, behind combined)
    // LF Combined curve (boost + atten interaction)
    if (cachedParams.lfBoostGain > 0.1f || cachedParams.lfAttenGain > 0.1f)
    {
        drawBandCurve(g, graphArea, juce::Colour(lfBoostColor).withAlpha(0.5f),
                      [this](float f) { return calculateLFCombinedResponse(f); });
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

void TubeEQCurveDisplay::resized()
{
    // Position analyzer within graph area (matching margins from paint())
    const float leftMargin = 30.0f;
    const float bottomMargin = 18.0f;
    const float topMargin = 6.0f;
    const float rightMargin = 6.0f;

    auto graphArea = getLocalBounds().toFloat();
    graphArea.removeFromLeft(leftMargin);
    graphArea.removeFromBottom(bottomMargin);
    graphArea.removeFromTop(topMargin);
    graphArea.removeFromRight(rightMargin);

    if (analyzer)
    {
        analyzer->setBounds(graphArea.toNearestInt());
        analyzer->setFrequencyRange(minFreq, maxFreq);
        analyzer->setDisplayRange(minDB, maxDB);
    }

    needsRepaint = true;
    repaint();  // Force immediate repaint when bounds change
}

void TubeEQCurveDisplay::timerCallback()
{
    // Update analyzer data from processor
    if (analyzer && audioProcessor.isAnalyzerDataReady())
    {
        analyzer->updateMagnitudes(audioProcessor.getAnalyzerMagnitudes());
        audioProcessor.clearAnalyzerDataReady();
    }

    // Check if parameters have changed - use Tube EQ mode parameter IDs
    auto& params = audioProcessor.parameters;

    CachedParams newParams;

    // Frequency lookup tables (must match MultiQ::processBlock Tube EQ section)
    static const float lfFreqValues[] = { 20.0f, 30.0f, 60.0f, 100.0f };
    static const float hfBoostFreqValues[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
    static const float hfAttenFreqValues[] = { 5000.0f, 10000.0f, 20000.0f };

    // Read Tube EQ mode parameters (freq params are indices, convert to Hz)
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecLfBoostGain))
        newParams.lfBoostGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecLfBoostFreq))
    {
        int idx = juce::jlimit(0, 3, static_cast<int>(p->load()));
        newParams.lfBoostFreq = lfFreqValues[idx];
    }
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecLfAttenGain))
        newParams.lfAttenGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfBoostGain))
        newParams.hfBoostGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfBoostFreq))
    {
        int idx = juce::jlimit(0, 6, static_cast<int>(p->load()));
        newParams.hfBoostFreq = hfBoostFreqValues[idx];
    }
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfBoostBandwidth))
        newParams.hfBoostBandwidth = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfAttenGain))
        newParams.hfAttenGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::pultecHfAttenFreq))
    {
        int idx = juce::jlimit(0, 2, static_cast<int>(p->load()));
        newParams.hfAttenFreq = hfAttenFreqValues[idx];
    }
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

float TubeEQCurveDisplay::freqToX(float freq, const juce::Rectangle<float>& area) const
{
    // Logarithmic frequency to X position within graph area
    float logMin = std::log10(minFreq);
    float logMax = std::log10(maxFreq);
    float logFreq = std::log10(std::max(freq, minFreq));

    float normalized = (logFreq - logMin) / (logMax - logMin);
    return area.getX() + area.getWidth() * normalized;
}

float TubeEQCurveDisplay::xToFreq(float x, const juce::Rectangle<float>& area) const
{
    float logMin = std::log10(minFreq);
    float logMax = std::log10(maxFreq);
    float normalized = (x - area.getX()) / area.getWidth();
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    return std::pow(10.0f, logMin + normalized * (logMax - logMin));
}

float TubeEQCurveDisplay::dbToY(float db, const juce::Rectangle<float>& area) const
{
    // dB to Y position (inverted - higher dB = lower Y)
    float normalized = (db - minDB) / (maxDB - minDB);
    return area.getBottom() - area.getHeight() * normalized;
}

void TubeEQCurveDisplay::drawVintageGrid(juce::Graphics& g, const juce::Rectangle<float>& area)
{
    // Vertical grid lines at key frequencies (tube EQ-relevant)
    // Emphasize the tube EQ frequency selections
    const float freqLines[] = { 30.0f, 60.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                 3000.0f, 5000.0f, 8000.0f, 10000.0f, 16000.0f };

    for (float freq : freqLines)
    {
        float x = freqToX(freq, area);
        // Highlight key frequencies
        bool isTubeEQFreq = (freq == 30.0f || freq == 60.0f || freq == 100.0f ||
                            freq == 3000.0f || freq == 5000.0f || freq == 8000.0f ||
                            freq == 10000.0f || freq == 16000.0f);

        g.setColour(juce::Colour(isTubeEQFreq ? 0xff404040 : 0xff303030));
        g.drawLine(x, area.getY(), x, area.getBottom(), isTubeEQFreq ? 1.0f : 0.5f);
    }

    // Horizontal grid lines — compute from current minDB/maxDB
    // Choose a sensible tick step based on the dB range
    float dbRange = maxDB - minDB;
    float tickStep;
    if (dbRange <= 24.0f)
        tickStep = 6.0f;
    else if (dbRange <= 48.0f)
        tickStep = 10.0f;
    else
        tickStep = 20.0f;

    // Generate ticks from minDB to maxDB at the chosen step, always including 0 dB
    for (float db = std::ceil(minDB / tickStep) * tickStep; db <= maxDB; db += tickStep)
    {
        float y = dbToY(db, area);
        bool isZero = (std::abs(db) < 0.1f);

        g.setColour(juce::Colour(isZero ? 0xff505050 : 0xff353535));
        g.drawLine(area.getX(), y, area.getRight(), y, isZero ? 1.5f : 0.5f);
    }

    // Frequency labels at bottom
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.setColour(juce::Colour(0xff909090));  // Light gray text

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

    // dB labels on left — dynamic based on current range
    auto drawDbLabel = [&](float db) {
        float y = dbToY(db, area);
        juce::String text;
        if (db > 0.0f)
            text = "+" + juce::String(static_cast<int>(db));
        else
            text = juce::String(static_cast<int>(db));
        g.drawText(text, 0, static_cast<int>(y) - 7, 28, 14, juce::Justification::right);
    };

    for (float db = std::ceil(minDB / tickStep) * tickStep; db <= maxDB; db += tickStep)
    {
        drawDbLabel(db);
    }
}

void TubeEQCurveDisplay::drawBandCurve(juce::Graphics& g, const juce::Rectangle<float>& area,
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

void TubeEQCurveDisplay::drawCombinedCurve(juce::Graphics& g, const juce::Rectangle<float>& area)
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

// Filter response calculations matching passive tube EQ characteristics

float TubeEQCurveDisplay::calculateLFCombinedResponse(float freq) const
{
    if (cachedParams.lfBoostGain < 0.1f && cachedParams.lfAttenGain < 0.1f)
        return 0.0f;

    // Replicate PultecLFSection::getMagnitudeDB using the same dual-biquad math
    // so the curve display matches the actual DSP processing.
    float boostGain = cachedParams.lfBoostGain;
    float attenGain = cachedParams.lfAttenGain;
    float frequency = cachedParams.lfBoostFreq;
    double sampleRate = audioProcessor.getSampleRate();
    if (sampleRate <= 0.0) sampleRate = 44100.0;

    float maxFreq = static_cast<float>(sampleRate) * 0.45f;
    frequency = std::clamp(frequency, 10.0f, maxFreq);

    const float twoPi = juce::MathConstants<float>::twoPi;
    double omega = juce::MathConstants<double>::twoPi * freq / sampleRate;
    double cosw = std::cos(omega);
    double sinw = std::sin(omega);
    double cos2w = std::cos(2.0 * omega);
    double sin2w = std::sin(2.0 * omega);

    auto biquadMag = [&](float b0, float b1, float b2, float a1, float a2) -> double
    {
        double numR = b0 + b1 * cosw + b2 * cos2w;
        double numI = -(b1 * sinw + b2 * sin2w);
        double denR = 1.0 + a1 * cosw + a2 * cos2w;
        double denI = -(a1 * sinw + a2 * sin2w);
        double numMag2 = numR * numR + numI * numI;
        double denMag2 = denR * denR + denI * denI;
        return (denMag2 > 1e-20) ? std::sqrt(numMag2 / denMag2) : 1.0;
    };

    // PultecLFSection constants
    // Must match PultecLFSection constants in TubeEQProcessor.h
    constexpr float kPeakGainScale = 1.4f;
    constexpr float kPeakInteraction = 0.08f;
    constexpr float kBaseQ = 0.55f;
    constexpr float kQInteraction = 0.015f;
    constexpr float kDipFreqBase = 1.0f;          // Same freq as boost (Pultec Trick)
    constexpr float kDipFreqRange = 0.0f;          // No gain-dependent shift
    constexpr float kDipGainScale = 1.75f;         // ~17.5 dB max (hardware match)
    constexpr float kDipInteraction = 0.06f;
    constexpr float kDipBaseQ = 0.65f;             // Broader shelf for Pultec Trick
    constexpr float kDipQScale = 0.03f;

    double peakMag = 1.0;
    double dipMag = 1.0;

    // Peak filter coefficients
    if (boostGain > 0.01f)
    {
        float peakGainDB = boostGain * kPeakGainScale + attenGain * boostGain * kPeakInteraction;
        // Approximate Q (without inductor model, use base Q with interaction)
        float effectiveQ = kBaseQ * (1.0f + attenGain * kQInteraction);
        effectiveQ = std::max(effectiveQ, 0.2f);

        float A = std::pow(10.0f, peakGainDB / 40.0f);
        float w0 = twoPi * frequency / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * effectiveQ);

        float pb0 = 1.0f + alpha * A;
        float pb1 = -2.0f * cosw0;
        float pb2 = 1.0f - alpha * A;
        float pa0 = 1.0f + alpha / A;
        float pa1 = -2.0f * cosw0;
        float pa2 = 1.0f - alpha / A;

        peakMag = biquadMag(pb0/pa0, pb1/pa0, pb2/pa0, pa1/pa0, pa2/pa0);
    }

    // Dip shelf coefficients
    if (attenGain > 0.01f)
    {
        float dipFreqRatio = kDipFreqBase + kDipFreqRange * (1.0f - attenGain / 10.0f);
        float dipFreq = frequency * dipFreqRatio;
        dipFreq = std::clamp(dipFreq, 10.0f, maxFreq);

        float dipGainDB = -(attenGain * kDipGainScale + boostGain * attenGain * kDipInteraction);
        float dipQ = kDipBaseQ + attenGain * kDipQScale;

        float A = std::pow(10.0f, dipGainDB / 40.0f);
        float w0 = twoPi * dipFreq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * dipQ);
        float sqrtA = std::sqrt(A);

        float db0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
        float db1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
        float db2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
        float da0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        float da1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
        float da2 = (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha;

        dipMag = biquadMag(db0/da0, db1/da0, db2/da0, da1/da0, da2/da0);
    }

    double combined = peakMag * dipMag;
    return static_cast<float>(20.0 * std::log10(combined + 1e-10));
}

float TubeEQCurveDisplay::calculateHFBoostResponse(float freq) const
{
    if (cachedParams.hfBoostGain < 0.1f)
        return 0.0f;

    float fc = cachedParams.hfBoostFreq;
    float gain = cachedParams.hfBoostGain * 1.8f;  // 0-10 maps to ~0-18 dB (hardware match)

    // Bandwidth control: Sharp (high Q) to Broad (low Q)
    // bandwidth 0 = sharp (narrow), 1 = broad (wide)
    float q = juce::jmap(cachedParams.hfBoostBandwidth, 0.0f, 1.0f, 2.0f, 0.3f);
    float bandwidth = 1.0f / q;

    // Peak filter response
    float logRatio = std::log(freq / fc);
    return gain * std::exp(-0.5f * std::pow(logRatio / (bandwidth * 0.6f), 2.0f));
}

float TubeEQCurveDisplay::calculateHFAttenResponse(float freq) const
{
    if (cachedParams.hfAttenGain < 0.1f)
        return 0.0f;

    float fc = cachedParams.hfAttenFreq;
    float gain = -cachedParams.hfAttenGain * 1.6f;  // 0-10 maps to ~0-16 dB cut (hardware match)

    // High shelf attenuation
    float logRatio = std::log10(freq / fc);

    // Sigmoid-like transition for shelf
    float transitionWidth = 0.5f;
    float normalized = 0.5f * (1.0f + std::tanh(logRatio / transitionWidth));

    return gain * normalized;
}

float TubeEQCurveDisplay::calculateCombinedResponse(float freq) const
{
    float response = 0.0f;

    // LF section: combined boost/cut interaction
    response += calculateLFCombinedResponse(freq);

    // HF sections
    response += calculateHFBoostResponse(freq);
    response += calculateHFAttenResponse(freq);

    return response;
}

void TubeEQCurveDisplay::setAnalyzerVisible(bool visible)
{
    if (analyzer)
    {
        analyzer->setVisible(visible);
        analyzer->setEnabled(visible);
    }
}

void TubeEQCurveDisplay::setAnalyzerSmoothingMode(FFTAnalyzer::SmoothingMode mode)
{
    if (analyzer)
        analyzer->setSmoothingMode(mode);
}

void TubeEQCurveDisplay::toggleSpectrumFreeze()
{
    if (analyzer)
    {
        analyzer->toggleFreeze();
        repaint();
    }
}

bool TubeEQCurveDisplay::isSpectrumFrozen() const
{
    return analyzer ? analyzer->isFrozen() : false;
}

void TubeEQCurveDisplay::setDisplayScaleMode(DisplayScaleMode mode)
{
    switch (mode)
    {
        case DisplayScaleMode::Linear12dB:
            minDB = -12.0f;
            maxDB = 12.0f;
            break;
        case DisplayScaleMode::Linear24dB:
            minDB = -24.0f;
            maxDB = 24.0f;
            break;
        case DisplayScaleMode::Linear30dB:
            minDB = -30.0f;
            maxDB = 30.0f;
            break;
        case DisplayScaleMode::Linear60dB:
            minDB = -60.0f;
            maxDB = 60.0f;
            break;
        case DisplayScaleMode::Warped:
            // Warped mode uses same range as 24dB for now
            minDB = -24.0f;
            maxDB = 24.0f;
            break;
        default:
            // Safe fallback for unknown enum values
            minDB = -24.0f;
            maxDB = 24.0f;
            break;
    }

    if (analyzer)
        analyzer->setDisplayRange(minDB, maxDB);

    needsRepaint = true;
    repaint();
}

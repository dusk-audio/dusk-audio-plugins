#include "BritishEQCurveDisplay.h"
#include <cmath>

BritishEQCurveDisplay::BritishEQCurveDisplay(MultiQ& processor)
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

BritishEQCurveDisplay::~BritishEQCurveDisplay()
{
    stopTimer();
}

void BritishEQCurveDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Early exit if bounds are invalid
    if (bounds.getWidth() < 10 || bounds.getHeight() < 10)
        return;

    // Define drawing area with margins for labels
    auto graphArea = bounds;
    graphArea.removeFromLeft(graphLeftMargin);
    graphArea.removeFromBottom(graphBottomMargin);
    graphArea.removeFromTop(graphTopMargin);
    graphArea.removeFromRight(graphRightMargin);

    // Background - slightly different from main background for visibility
    g.setColour(juce::Colour(0xff151518));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Inner darker area for the graph
    g.setColour(juce::Colour(0xff101014));
    g.fillRoundedRectangle(graphArea.reduced(1), 3.0f);

    // Border
    g.setColour(juce::Colour(0xff404040));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    // Draw grid within graph area
    drawGrid(g, graphArea);

    // Save graphics state before clipping
    g.saveState();

    // Clip to graph area for curves
    g.reduceClipRegion(graphArea.reduced(2).toNearestInt());

    // Always draw combined curve (even if flat at 0dB)
    drawCombinedCurve(g, graphArea);

    // Draw individual band curves (subtle, behind combined) - only if active
    // HPF curve (subtle grey) - only show if enabled
    if (cachedParams.hpfEnabled)
    {
        drawBandCurve(g, graphArea, juce::Colour(filterColor).withAlpha(0.5f),
                      [this](float f) { return calculateHPFResponse(f); });
    }

    // LPF curve (subtle grey) - only show if enabled
    if (cachedParams.lpfEnabled)
    {
        drawBandCurve(g, graphArea, juce::Colour(filterColor).withAlpha(0.5f),
                      [this](float f) { return calculateLPFResponse(f); });
    }

    // LF band (red)
    if (std::abs(cachedParams.lfGain) > 0.5f)
    {
        drawBandCurve(g, graphArea, juce::Colour(bandLFColor).withAlpha(0.5f),
                      [this](float f) { return calculateLFResponse(f); });
    }

    // LMF band (orange)
    if (std::abs(cachedParams.lmGain) > 0.5f)
    {
        drawBandCurve(g, graphArea, juce::Colour(bandLMFColor).withAlpha(0.5f),
                      [this](float f) { return calculateLMFResponse(f); });
    }

    // HMF band (green)
    if (std::abs(cachedParams.hmGain) > 0.5f)
    {
        drawBandCurve(g, graphArea, juce::Colour(bandHMFColor).withAlpha(0.5f),
                      [this](float f) { return calculateHMFResponse(f); });
    }

    // HF band (blue)
    if (std::abs(cachedParams.hfGain) > 0.5f)
    {
        drawBandCurve(g, graphArea, juce::Colour(bandHFColor).withAlpha(0.5f),
                      [this](float f) { return calculateHFResponse(f); });
    }

    // Restore graphics state
    g.restoreState();

    // Frozen spectrum indicator
    if (isSpectrumFrozen())
    {
        juce::String frozenText = "FROZEN (F)";
        auto font = juce::FontOptions(11.0f, juce::Font::bold);
        g.setFont(font);
        float textWidth = g.getCurrentFont().getStringWidth(frozenText) + 12.0f;
        float textHeight = 18.0f;
        float badgeX = graphArea.getX() + 6.0f;
        float badgeY = graphArea.getY() + 6.0f;

        juce::Rectangle<float> badgeRect(badgeX, badgeY, textWidth, textHeight);
        g.setColour(juce::Colour(0xCC2e1a1a));
        g.fillRoundedRectangle(badgeRect, 4.0f);
        g.setColour(juce::Colour(0x6000ccff));
        g.drawRoundedRectangle(badgeRect, 4.0f, 1.0f);

        g.setColour(juce::Colour(0xDD00ccff));
        g.drawText(frozenText, badgeRect, juce::Justification::centred);
    }
}

void BritishEQCurveDisplay::resized()
{
    // Position analyzer within graph area (using class-level margin constants)
    auto graphArea = getLocalBounds().toFloat();
    graphArea.removeFromLeft(graphLeftMargin);
    graphArea.removeFromBottom(graphBottomMargin);
    graphArea.removeFromTop(graphTopMargin);
    graphArea.removeFromRight(graphRightMargin);

    if (analyzer)
    {
        analyzer->setBounds(graphArea.toNearestInt());
        analyzer->setFrequencyRange(minFreq, maxFreq);
        analyzer->setDisplayRange(minDisplayDB, maxDisplayDB);
    }

    needsRepaint = true;
    repaint();  // Force immediate repaint when bounds change
}

void BritishEQCurveDisplay::timerCallback()
{
    // Update analyzer data from processor
    if (analyzer && audioProcessor.isAnalyzerDataReady())
    {
        analyzer->updateMagnitudes(audioProcessor.getAnalyzerMagnitudes());
        audioProcessor.clearAnalyzerDataReady();
    }

    // Check if parameters have changed - use British mode parameter IDs
    auto& params = audioProcessor.parameters;

    CachedParams newParams;

    // Read British mode parameters
    if (auto* p = params.getRawParameterValue(ParamIDs::britishHpfFreq))
        newParams.hpfFreq = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishHpfEnabled))
        newParams.hpfEnabled = p->load() > 0.5f;
    if (auto* p = params.getRawParameterValue(ParamIDs::britishLpfFreq))
        newParams.lpfFreq = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishLpfEnabled))
        newParams.lpfEnabled = p->load() > 0.5f;
    if (auto* p = params.getRawParameterValue(ParamIDs::britishLfGain))
        newParams.lfGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishLfFreq))
        newParams.lfFreq = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishLfBell))
        newParams.lfBell = p->load() > 0.5f;
    if (auto* p = params.getRawParameterValue(ParamIDs::britishLmGain))
        newParams.lmGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishLmFreq))
        newParams.lmFreq = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishLmQ))
        newParams.lmQ = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishHmGain))
        newParams.hmGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishHmFreq))
        newParams.hmFreq = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishHmQ))
        newParams.hmQ = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishHfGain))
        newParams.hfGain = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishHfFreq))
        newParams.hfFreq = p->load();
    if (auto* p = params.getRawParameterValue(ParamIDs::britishHfBell))
        newParams.hfBell = p->load() > 0.5f;
    if (auto* p = params.getRawParameterValue(ParamIDs::britishMode))
        newParams.isBlack = p->load() > 0.5f;

    // Compare using epsilon for floating point values
    auto floatsDiffer = [](float a, float b) {
        return std::abs(a - b) > 0.001f;
    };

    bool changed = floatsDiffer(newParams.hpfFreq, cachedParams.hpfFreq) ||
                   (newParams.hpfEnabled != cachedParams.hpfEnabled) ||
                   floatsDiffer(newParams.lpfFreq, cachedParams.lpfFreq) ||
                   (newParams.lpfEnabled != cachedParams.lpfEnabled) ||
                   floatsDiffer(newParams.lfGain, cachedParams.lfGain) ||
                   floatsDiffer(newParams.lfFreq, cachedParams.lfFreq) ||
                   (newParams.lfBell != cachedParams.lfBell) ||
                   floatsDiffer(newParams.lmGain, cachedParams.lmGain) ||
                   floatsDiffer(newParams.lmFreq, cachedParams.lmFreq) ||
                   floatsDiffer(newParams.lmQ, cachedParams.lmQ) ||
                   floatsDiffer(newParams.hmGain, cachedParams.hmGain) ||
                   floatsDiffer(newParams.hmFreq, cachedParams.hmFreq) ||
                   floatsDiffer(newParams.hmQ, cachedParams.hmQ) ||
                   floatsDiffer(newParams.hfGain, cachedParams.hfGain) ||
                   floatsDiffer(newParams.hfFreq, cachedParams.hfFreq) ||
                   (newParams.hfBell != cachedParams.hfBell) ||
                   (newParams.isBlack != cachedParams.isBlack);

    if (changed || needsRepaint)
    {
        cachedParams = newParams;
        needsRepaint = false;
        repaint();
    }
}

float BritishEQCurveDisplay::freqToX(float freq, const juce::Rectangle<float>& area) const
{
    // Logarithmic frequency to X position within graph area
    float logMin = std::log10(minFreq);
    float logMax = std::log10(maxFreq);
    float logFreq = std::log10(std::max(freq, minFreq));

    float normalized = (logFreq - logMin) / (logMax - logMin);
    return area.getX() + area.getWidth() * normalized;
}

float BritishEQCurveDisplay::xToFreq(float x, const juce::Rectangle<float>& area) const
{
    float logMin = std::log10(minFreq);
    float logMax = std::log10(maxFreq);
    float normalized = (x - area.getX()) / area.getWidth();
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    return std::pow(10.0f, logMin + normalized * (logMax - logMin));
}

float BritishEQCurveDisplay::dbToY(float db, const juce::Rectangle<float>& area) const
{
    // dB to Y position (inverted - higher dB = lower Y)
    // For warped mode, apply logarithmic scaling for better small adjustment visualization
    if (scaleMode == BritishDisplayScaleMode::Warped)
    {
        // Warped scaling: compress middle range, expand extremes
        float sign = (db >= 0.0f) ? 1.0f : -1.0f;
        float absDb = std::abs(db);
        // Apply sqrt scaling for smoother transition
        float warpedDb = sign * std::sqrt(absDb / maxDisplayDB) * maxDisplayDB;
        float normalized = (warpedDb - minDisplayDB) / (maxDisplayDB - minDisplayDB);
        return area.getBottom() - area.getHeight() * normalized;
    }

    float normalized = (db - minDisplayDB) / (maxDisplayDB - minDisplayDB);
    return area.getBottom() - area.getHeight() * normalized;
}

void BritishEQCurveDisplay::drawGrid(juce::Graphics& g, const juce::Rectangle<float>& area)
{
    // Vertical grid lines at key frequencies
    const float freqLines[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };

    for (float freq : freqLines)
    {
        float x = freqToX(freq, area);
        bool isMajor = (freq == 100.0f || freq == 1000.0f || freq == 10000.0f);

        g.setColour(juce::Colour(isMajor ? 0xff2d2d2d : 0xff232323));
        g.drawLine(x, area.getY(), x, area.getBottom(), isMajor ? 1.0f : 0.5f);
    }

    // Horizontal grid lines - calculate step based on scale mode (matching 4K-EQ)
    float dbStep;
    switch (scaleMode) {
        case BritishDisplayScaleMode::Linear12dB:
            dbStep = 6.0f;
            break;
        case BritishDisplayScaleMode::Linear60dB:
            dbStep = 20.0f;
            break;
        default:
            dbStep = 10.0f;
            break;
    }

    // Draw grid lines from min to max
    for (float db = minDisplayDB; db <= maxDisplayDB; db += dbStep)
    {
        float y = dbToY(db, area);
        bool isZero = (std::abs(db) < 0.1f);

        g.setColour(juce::Colour(isZero ? 0xff404040 : 0xff2a2a2a));
        g.drawLine(area.getX(), y, area.getRight(), y, isZero ? 1.5f : 0.5f);
    }

    // Frequency labels at bottom
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.setColour(juce::Colour(0xff707070));

    auto drawFreqLabel = [&](float freq, const juce::String& text) {
        float x = freqToX(freq, area);
        g.drawText(text, static_cast<int>(x) - 18, static_cast<int>(area.getBottom()) + 3, 36, 14,
                   juce::Justification::centred);
    };

    drawFreqLabel(100.0f, "100");
    drawFreqLabel(1000.0f, "1k");
    drawFreqLabel(10000.0f, "10k");

    // dB labels on left - show at appropriate intervals based on scale mode
    auto drawDbLabel = [&](float db, const juce::String& text) {
        float y = dbToY(db, area);
        g.drawText(text, 4, static_cast<int>(y) - 7, 24, 14, juce::Justification::right);
    };

    // Always show 0dB, then show max and min
    drawDbLabel(0.0f, "0");

    if (scaleMode == BritishDisplayScaleMode::Linear12dB)
    {
        drawDbLabel(12.0f, "+12");
        drawDbLabel(-12.0f, "-12");
    }
    else if (scaleMode == BritishDisplayScaleMode::Linear60dB)
    {
        drawDbLabel(60.0f, "+60");
        drawDbLabel(-60.0f, "-60");
        drawDbLabel(30.0f, "+30");
        drawDbLabel(-30.0f, "-30");
    }
    else
    {
        // ±24 or ±30
        float labelDb = (scaleMode == BritishDisplayScaleMode::Linear24dB) ? 20.0f : 30.0f;
        drawDbLabel(labelDb, "+" + juce::String(static_cast<int>(labelDb)));
        drawDbLabel(-labelDb, juce::String(static_cast<int>(-labelDb)));
    }
}

void BritishEQCurveDisplay::drawBandCurve(juce::Graphics& g, const juce::Rectangle<float>& area,
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

        // Clamp dB to visible range (strict clamping to prevent drawing outside bounds)
        db = juce::jlimit(minDisplayDB, maxDisplayDB, db);

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

void BritishEQCurveDisplay::drawCombinedCurve(juce::Graphics& g, const juce::Rectangle<float>& area)
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

        // Clamp dB to visible range (strict clamping to prevent drawing outside bounds)
        db = juce::jlimit(minDisplayDB, maxDisplayDB, db);

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

    // Draw subtle fill between curve and 0dB line
    g.setColour(juce::Colour(combinedColor).withAlpha(0.12f));
    g.fillPath(fillPath);

    // Draw glow effect (multiple passes for soft glow)
    g.setColour(juce::Colour(combinedColor).withAlpha(0.08f));
    g.strokePath(path, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(juce::Colour(combinedColor).withAlpha(0.15f));
    g.strokePath(path, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Draw main line (bright and visible)
    g.setColour(juce::Colour(combinedColor));
    g.strokePath(path, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Filter response calculations
// These are simplified approximations for display purposes

float BritishEQCurveDisplay::calculateHPFResponse(float freq) const
{
    if (!cachedParams.hpfEnabled)
        return 0.0f;  // HPF disabled - no effect on response

    // 3rd order HPF (18dB/oct) with smooth transition at cutoff
    float ratio = freq / cachedParams.hpfFreq;
    if (ratio < 0.001f) ratio = 0.001f;

    float response;
    if (ratio < 1.0f)
    {
        // 18dB/octave = 18 * log10(ratio)
        response = 18.0f * std::log10(ratio);
    }
    else
    {
        // Above cutoff, gradual transition to 0dB
        response = 0.0f;
    }

    return juce::jlimit(-60.0f, 0.0f, response);
}

float BritishEQCurveDisplay::calculateLPFResponse(float freq) const
{
    if (!cachedParams.lpfEnabled)
        return 0.0f;  // LPF disabled - no effect on response

    // 2nd order LPF (12dB/oct)
    float ratio = freq / cachedParams.lpfFreq;
    if (ratio < 0.001f) ratio = 0.001f;

    float response;
    if (ratio > 1.0f)
    {
        // 12dB/octave rolloff above cutoff
        response = -12.0f * std::log10(ratio);
    }
    else
    {
        response = 0.0f;
    }

    return juce::jlimit(-60.0f, 0.0f, response);
}

float BritishEQCurveDisplay::calculateLFResponse(float freq) const
{
    if (std::abs(cachedParams.lfGain) < 0.1f)
        return 0.0f;

    float fc = cachedParams.lfFreq;
    float gain = cachedParams.lfGain;
    bool isBell = cachedParams.lfBell && cachedParams.isBlack;

    if (isBell)
    {
        // Bell/peak filter response using Gaussian approximation
        float q = 0.7f;
        float logRatio = std::log(freq / fc);
        float bandwidth = 1.0f / q;

        return gain * std::exp(-0.5f * std::pow(logRatio / (bandwidth * 0.5f), 2.0f));
    }
    else
    {
        // Low shelf response - smoother S-curve transition
        float logRatio = std::log10(freq / fc);

        // Sigmoid-like transition centered at fc
        float transitionWidth = 0.5f;
        float normalized = 0.5f * (1.0f + std::tanh(-logRatio / transitionWidth));

        return gain * normalized;
    }
}

float BritishEQCurveDisplay::calculateLMFResponse(float freq) const
{
    if (std::abs(cachedParams.lmGain) < 0.1f)
        return 0.0f;

    float fc = cachedParams.lmFreq;
    float gain = cachedParams.lmGain;
    float q = cachedParams.lmQ;

    // Peak filter response using Gaussian approximation
    float logRatio = std::log(freq / fc);
    float bandwidth = 1.0f / q;

    return gain * std::exp(-0.5f * std::pow(logRatio / (bandwidth * 0.5f), 2.0f));
}

float BritishEQCurveDisplay::calculateHMFResponse(float freq) const
{
    if (std::abs(cachedParams.hmGain) < 0.1f)
        return 0.0f;

    float fc = cachedParams.hmFreq;
    float gain = cachedParams.hmGain;
    float q = cachedParams.hmQ;

    // Peak filter response using Gaussian approximation
    float logRatio = std::log(freq / fc);
    float bandwidth = 1.0f / q;

    return gain * std::exp(-0.5f * std::pow(logRatio / (bandwidth * 0.5f), 2.0f));
}

float BritishEQCurveDisplay::calculateHFResponse(float freq) const
{
    if (std::abs(cachedParams.hfGain) < 0.1f)
        return 0.0f;

    float fc = cachedParams.hfFreq;
    float gain = cachedParams.hfGain;
    bool isBell = cachedParams.hfBell && cachedParams.isBlack;

    if (isBell)
    {
        // Bell/peak filter response
        float q = 0.7f;
        float logRatio = std::log(freq / fc);
        float bandwidth = 1.0f / q;

        return gain * std::exp(-0.5f * std::pow(logRatio / (bandwidth * 0.5f), 2.0f));
    }
    else
    {
        // High shelf response - smoother S-curve transition
        float logRatio = std::log10(freq / fc);

        // Sigmoid-like transition centered at fc
        float transitionWidth = 0.5f;
        float normalized = 0.5f * (1.0f + std::tanh(logRatio / transitionWidth));

        return gain * normalized;
    }
}

float BritishEQCurveDisplay::calculateCombinedResponse(float freq) const
{
    float response = 0.0f;

    // Add all band responses
    response += calculateHPFResponse(freq);
    response += calculateLPFResponse(freq);
    response += calculateLFResponse(freq);
    response += calculateLMFResponse(freq);
    response += calculateHMFResponse(freq);
    response += calculateHFResponse(freq);

    return response;
}

void BritishEQCurveDisplay::setAnalyzerVisible(bool visible)
{
    if (analyzer)
    {
        analyzer->setVisible(visible);
        analyzer->setEnabled(visible);
    }
}

void BritishEQCurveDisplay::setDisplayScaleMode(BritishDisplayScaleMode mode)
{
    scaleMode = mode;
    switch (mode) {
        case BritishDisplayScaleMode::Linear12dB:
            minDisplayDB = -12.0f;
            maxDisplayDB = 12.0f;
            break;
        case BritishDisplayScaleMode::Linear24dB:
            minDisplayDB = -24.0f;
            maxDisplayDB = 24.0f;
            break;
        case BritishDisplayScaleMode::Linear30dB:
            minDisplayDB = -30.0f;
            maxDisplayDB = 30.0f;
            break;
        case BritishDisplayScaleMode::Linear60dB:
            minDisplayDB = -60.0f;
            maxDisplayDB = 60.0f;
            break;
        case BritishDisplayScaleMode::Warped:
            // Warped mode uses ±30dB but with logarithmic scaling
            minDisplayDB = -30.0f;
            maxDisplayDB = 30.0f;
            break;
    }
    if (analyzer)
        analyzer->setDisplayRange(minDisplayDB, maxDisplayDB);

    needsRepaint = true;
    repaint();
}

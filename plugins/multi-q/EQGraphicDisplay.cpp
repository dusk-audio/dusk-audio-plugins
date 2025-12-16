#include "EQGraphicDisplay.h"
#include "MultiQ.h"

//==============================================================================
EQGraphicDisplay::EQGraphicDisplay(MultiQ& proc)
    : processor(proc)
{
    // Create analyzer component
    analyzer = std::make_unique<FFTAnalyzer>();
    addAndMakeVisible(analyzer.get());
    analyzer->setFillColor(juce::Colour(0x30888888));
    analyzer->setLineColor(juce::Colour(0x80AAAAAA));

    // Start timer for UI updates
    startTimerHz(30);
}

EQGraphicDisplay::~EQGraphicDisplay()
{
    stopTimer();
}

//==============================================================================
void EQGraphicDisplay::timerCallback()
{
    // Update analyzer data
    if (processor.isAnalyzerDataReady())
    {
        analyzer->updateMagnitudes(processor.getAnalyzerMagnitudes());
        processor.clearAnalyzerDataReady();
    }

    repaint();
}

//==============================================================================
void EQGraphicDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark background
    g.setColour(juce::Colour(0xFF1a1a1a));
    g.fillRect(bounds);

    // Draw grid
    drawGrid(g);

    // Draw individual band curves (with fill)
    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
    {
        if (isBandEnabled(i))
            drawBandCurve(g, i);
    }

    // Draw combined EQ curve
    drawCombinedCurve(g);

    // Draw master gain overlay if enabled
    if (showMasterGain && std::abs(masterGainDB) > 0.01f)
        drawMasterGainOverlay(g);

    // Draw control points
    drawControlPoints(g);

    // Draw border
    g.setColour(juce::Colour(0xFF333333));
    g.drawRect(bounds, 1.0f);
}

void EQGraphicDisplay::resized()
{
    // Analyzer fills the entire display area
    analyzer->setBounds(getLocalBounds().reduced(40, 20));
    analyzer->setFrequencyRange(minFrequency, maxFrequency);
    // Use a fixed spectrum analyzer range (-80 to 0 dB) independent of EQ display scale
    analyzer->setDisplayRange(-80.0f, 0.0f);
}

//==============================================================================
void EQGraphicDisplay::drawGrid(juce::Graphics& g)
{
    auto displayBounds = getDisplayBounds();

    // Minor frequency grid lines (lighter)
    g.setColour(juce::Colour(0xFF222222));
    std::array<float, 7> minorFreqLines = {20.0f, 50.0f, 150.0f, 300.0f, 700.0f, 3000.0f, 7000.0f};
    for (float freq : minorFreqLines)
    {
        float x = getXForFrequency(freq);
        if (x >= displayBounds.getX() && x <= displayBounds.getRight())
            g.drawVerticalLine(static_cast<int>(x), displayBounds.getY(), displayBounds.getBottom());
    }

    // Major frequency grid lines (brighter)
    g.setColour(juce::Colour(0xFF333333));
    std::array<float, 4> majorFreqLines = {100.0f, 1000.0f, 10000.0f, 20000.0f};
    for (float freq : majorFreqLines)
    {
        float x = getXForFrequency(freq);
        if (x >= displayBounds.getX() && x <= displayBounds.getRight())
            g.drawVerticalLine(static_cast<int>(x), displayBounds.getY(), displayBounds.getBottom());
    }

    // Draw dB grid lines
    float dbStep = 6.0f;
    if (scaleMode == DisplayScaleMode::Linear30dB) dbStep = 10.0f;
    if (scaleMode == DisplayScaleMode::Linear60dB) dbStep = 20.0f;

    for (float db = minDisplayDB; db <= maxDisplayDB; db += dbStep)
    {
        float y = getYForDB(db);
        if (std::abs(db) < 0.01f)  // 0 dB line - prominent
        {
            g.setColour(juce::Colour(0xFF555555));
            g.drawHorizontalLine(static_cast<int>(y), displayBounds.getX(), displayBounds.getRight());
        }
        else
        {
            g.setColour(juce::Colour(0xFF2a2a2a));
            g.drawHorizontalLine(static_cast<int>(y), displayBounds.getX(), displayBounds.getRight());
        }
    }

    // Draw frequency labels with better visibility
    g.setFont(juce::Font(juce::FontOptions(10.0f)));

    // Major frequency labels (brighter)
    std::array<std::pair<float, const char*>, 4> majorLabels = {{
        {100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"}, {20000.0f, "20k"}
    }};

    for (auto& [freq, label] : majorLabels)
    {
        float x = getXForFrequency(freq);
        g.setColour(juce::Colour(0xFF999999));
        g.drawText(label, static_cast<int>(x - 18), static_cast<int>(displayBounds.getBottom() + 3),
                   36, 14, juce::Justification::centred);
    }

    // Minor frequency labels (dimmer)
    std::array<std::pair<float, const char*>, 3> minorLabels = {{
        {20.0f, "20"}, {200.0f, "200"}, {2000.0f, "2k"}
    }};

    for (auto& [freq, label] : minorLabels)
    {
        float x = getXForFrequency(freq);
        g.setColour(juce::Colour(0xFF666666));
        g.drawText(label, static_cast<int>(x - 15), static_cast<int>(displayBounds.getBottom() + 3),
                   30, 14, juce::Justification::centred);
    }

    // Draw dB labels with better contrast
    for (float db = minDisplayDB; db <= maxDisplayDB; db += dbStep)
    {
        float y = getYForDB(db);
        juce::String label = (db > 0 ? "+" : "") + juce::String(static_cast<int>(db));

        // 0 dB label is brighter
        if (std::abs(db) < 0.01f)
            g.setColour(juce::Colour(0xFFAAAAAA));
        else
            g.setColour(juce::Colour(0xFF777777));

        g.drawText(label, 5, static_cast<int>(y - 7), 28, 14, juce::Justification::right);
    }
}

void EQGraphicDisplay::drawBandCurve(juce::Graphics& g, int bandIndex)
{
    auto displayBounds = getDisplayBounds();
    const auto& config = DefaultBandConfigs[static_cast<size_t>(bandIndex)];

    juce::Path curvePath;
    bool pathStarted = false;

    // Calculate band response at each x position
    int numPoints = static_cast<int>(displayBounds.getWidth());

    for (int px = 0; px < numPoints; ++px)
    {
        float x = displayBounds.getX() + static_cast<float>(px);
        float freq = getFrequencyAtX(x);

        // Get approximate magnitude response for this band only
        float response = 0.0f;
        float bandFreq = getBandFrequency(bandIndex);
        float gain = getBandGain(bandIndex);
        float q = processor.getEffectiveQ(bandIndex + 1);  // 1-indexed

        if (bandIndex == 0)  // HPF
        {
            float ratio = freq / bandFreq;
            if (ratio < 1.0f)
            {
                auto slopeParam = processor.parameters.getRawParameterValue(ParamIDs::bandSlope(1));
                int slopeIndex = slopeParam ? static_cast<int>(slopeParam->load()) : 1;
                float slopeDB = 6.0f * (slopeIndex + 1);
                response = slopeDB * std::log2(ratio);  // Negative for attenuation
            }
        }
        else if (bandIndex == 7)  // LPF
        {
            float ratio = bandFreq / freq;
            if (ratio < 1.0f)
            {
                auto slopeParam = processor.parameters.getRawParameterValue(ParamIDs::bandSlope(8));
                int slopeIndex = slopeParam ? static_cast<int>(slopeParam->load()) : 1;
                float slopeDB = 6.0f * (slopeIndex + 1);
                response = slopeDB * std::log2(ratio);
            }
        }
        else if (bandIndex == 1)  // Low Shelf
        {
            float ratio = freq / bandFreq;
            if (ratio < 0.5f)
                response = gain;
            else if (ratio < 2.0f)
            {
                float transition = (std::log2(ratio) + 1.0f) / 2.0f;  // 0 to 1 over one octave each side
                response = gain * (1.0f - transition);
            }
        }
        else if (bandIndex == 6)  // High Shelf
        {
            float ratio = freq / bandFreq;
            if (ratio > 2.0f)
                response = gain;
            else if (ratio > 0.5f)
            {
                float transition = (std::log2(ratio) + 1.0f) / 2.0f;
                response = gain * transition;
            }
        }
        else  // Parametric bands 3-6
        {
            float logRatio = std::log2(freq / bandFreq);
            float bandwidth = 1.0f / q;
            float envelope = std::exp(-logRatio * logRatio / (bandwidth * bandwidth * 0.5f));
            response = gain * envelope;
        }

        float y = getYForDB(response);

        if (!pathStarted)
        {
            curvePath.startNewSubPath(x, y);
            pathStarted = true;
        }
        else
        {
            curvePath.lineTo(x, y);
        }
    }

    // Create fill path
    juce::Path fillPath = curvePath;
    float zeroY = getYForDB(0.0f);
    fillPath.lineTo(displayBounds.getRight(), zeroY);
    fillPath.lineTo(displayBounds.getX(), zeroY);
    fillPath.closeSubPath();

    // Draw fill with band color (semi-transparent)
    juce::Colour bandColor = config.color;
    g.setColour(bandColor.withAlpha(0.15f));
    g.fillPath(fillPath);

    // Draw curve line
    bool isSelected = (bandIndex == selectedBand);
    g.setColour(bandColor.withAlpha(isSelected ? 1.0f : 0.6f));
    g.strokePath(curvePath, juce::PathStrokeType(isSelected ? 2.0f : 1.5f));
}

void EQGraphicDisplay::drawCombinedCurve(juce::Graphics& g)
{
    auto displayBounds = getDisplayBounds();

    juce::Path combinedPath;
    bool pathStarted = false;

    int numPoints = static_cast<int>(displayBounds.getWidth());

    for (int px = 0; px < numPoints; ++px)
    {
        float x = displayBounds.getX() + static_cast<float>(px);
        float freq = getFrequencyAtX(x);

        float response = processor.getFrequencyResponseMagnitude(freq);
        float y = getYForDB(response);

        if (!pathStarted)
        {
            combinedPath.startNewSubPath(x, y);
            pathStarted = true;
        }
        else
        {
            combinedPath.lineTo(x, y);
        }
    }

    // Draw combined curve with glow effect
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.strokePath(combinedPath, juce::PathStrokeType(4.0f));

    g.setColour(juce::Colours::white);
    g.strokePath(combinedPath, juce::PathStrokeType(2.0f));
}

void EQGraphicDisplay::drawControlPoints(juce::Graphics& g)
{
    // First draw inactive bands as faint indicators
    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
    {
        if (!isBandEnabled(i))
            drawInactiveBandIndicator(g, i);
    }

    // Then draw active bands on top
    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
    {
        if (isBandEnabled(i))
            drawBandControlPoint(g, i);
    }
}

void EQGraphicDisplay::drawInactiveBandIndicator(juce::Graphics& g, int bandIndex)
{
    auto point = getControlPointPosition(bandIndex);
    const auto& config = DefaultBandConfigs[static_cast<size_t>(bandIndex)];

    float radius = CONTROL_POINT_RADIUS * 0.6f;

    // Faint outline only
    g.setColour(config.color.withAlpha(0.25f));
    g.drawEllipse(point.x - radius, point.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // Very faint fill
    g.setColour(config.color.withAlpha(0.1f));
    g.fillEllipse(point.x - radius, point.y - radius, radius * 2.0f, radius * 2.0f);

    // Faint band number
    g.setColour(config.color.withAlpha(0.3f));
    g.setFont(juce::Font(juce::FontOptions(8.0f)));
    g.drawText(juce::String(bandIndex + 1),
               static_cast<int>(point.x - radius), static_cast<int>(point.y - radius),
               static_cast<int>(radius * 2.0f), static_cast<int>(radius * 2.0f),
               juce::Justification::centred);
}

void EQGraphicDisplay::drawBandControlPoint(juce::Graphics& g, int bandIndex)
{
    auto point = getControlPointPosition(bandIndex);
    const auto& config = DefaultBandConfigs[static_cast<size_t>(bandIndex)];

    bool isSelected = (bandIndex == selectedBand);
    bool isHovered = (bandIndex == hoveredBand);

    float radius = CONTROL_POINT_RADIUS;
    if (isSelected) radius *= 1.3f;
    else if (isHovered) radius *= 1.15f;

    // Outer glow for selected
    if (isSelected)
    {
        g.setColour(config.color.withAlpha(0.4f));
        g.fillEllipse(point.x - radius * 1.8f, point.y - radius * 1.8f,
                      radius * 3.6f, radius * 3.6f);
    }
    // Subtle glow for hovered
    else if (isHovered)
    {
        g.setColour(config.color.withAlpha(0.2f));
        g.fillEllipse(point.x - radius * 1.4f, point.y - radius * 1.4f,
                      radius * 2.8f, radius * 2.8f);
    }

    // Drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillEllipse(point.x - radius + 1.5f, point.y - radius + 1.5f, radius * 2.0f, radius * 2.0f);

    // Fill with gradient
    juce::ColourGradient gradient(config.color.brighter(0.2f), point.x, point.y - radius,
                                   config.color.darker(0.2f), point.x, point.y + radius, false);
    g.setGradientFill(gradient);
    g.fillEllipse(point.x - radius, point.y - radius, radius * 2.0f, radius * 2.0f);

    // Border - brighter for selected
    g.setColour(isSelected ? juce::Colours::white : juce::Colours::white.withAlpha(0.8f));
    g.drawEllipse(point.x - radius, point.y - radius, radius * 2.0f, radius * 2.0f, isSelected ? 2.0f : 1.5f);

    // Band number
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(isSelected ? 11.0f : 10.0f).withStyle("Bold")));
    g.drawText(juce::String(bandIndex + 1),
               static_cast<int>(point.x - radius), static_cast<int>(point.y - radius),
               static_cast<int>(radius * 2.0f), static_cast<int>(radius * 2.0f),
               juce::Justification::centred);
}

void EQGraphicDisplay::drawMasterGainOverlay(juce::Graphics& g)
{
    auto displayBounds = getDisplayBounds();

    float y = getYForDB(masterGainDB);
    float zeroY = getYForDB(0.0f);

    // Fill area between zero and master gain
    juce::Rectangle<float> gainArea;
    if (masterGainDB > 0)
        gainArea = juce::Rectangle<float>(displayBounds.getX(), y, displayBounds.getWidth(), zeroY - y);
    else
        gainArea = juce::Rectangle<float>(displayBounds.getX(), zeroY, displayBounds.getWidth(), y - zeroY);

    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRect(gainArea);

    // Draw master gain line
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawHorizontalLine(static_cast<int>(y), displayBounds.getX(), displayBounds.getRight());
}

//==============================================================================
void EQGraphicDisplay::mouseDown(const juce::MouseEvent& e)
{
    auto point = e.position;

    // Check if we clicked on a control point (including inactive ones for right-click)
    int hitBand = hitTestControlPoint(point);

    // Also check inactive bands for right-click enabling
    if (hitBand < 0)
    {
        for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
        {
            if (!isBandEnabled(i))
            {
                auto controlPoint = getControlPointPosition(i);
                float distance = point.getDistanceFrom(controlPoint);
                if (distance <= CONTROL_POINT_HIT_RADIUS * 1.2f)  // Slightly larger hit area for inactive
                {
                    hitBand = i;
                    break;
                }
            }
        }
    }

    // Right-click shows context menu
    if (e.mods.isRightButtonDown() && hitBand >= 0)
    {
        selectedBand = hitBand;
        if (onBandSelected)
            onBandSelected(selectedBand);
        repaint();
        showBandContextMenu(hitBand, e.getScreenPosition());
        return;
    }

    if (hitBand >= 0 && isBandEnabled(hitBand))
    {
        selectedBand = hitBand;
        isDragging = true;
        dragStartPoint = point;
        dragStartFreq = getBandFrequency(hitBand);
        dragStartGain = getBandGain(hitBand);
        dragStartQ = getBandQ(hitBand);

        // Determine drag mode based on modifiers
        if (e.mods.isAltDown() && e.mods.isCommandDown())
            currentDragMode = DragMode::QOnly;
        else if (e.mods.isCommandDown())
            currentDragMode = DragMode::GainOnly;
        else if (e.mods.isShiftDown())
            currentDragMode = DragMode::FrequencyOnly;
        else
            currentDragMode = DragMode::FrequencyAndGain;

        if (onBandSelected)
            onBandSelected(selectedBand);

        repaint();
    }
    else if (hitBand >= 0 && !isBandEnabled(hitBand))
    {
        // Clicked on inactive band - enable it
        setBandEnabled(hitBand, true);
        selectedBand = hitBand;
        if (onBandSelected)
            onBandSelected(selectedBand);
        repaint();
    }
    else
    {
        // Clicked on empty area - deselect
        selectedBand = -1;
        if (onBandSelected)
            onBandSelected(-1);
        repaint();
    }
}

void EQGraphicDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDragging || selectedBand < 0)
        return;

    auto point = e.position;
    auto displayBounds = getDisplayBounds();

    float deltaX = point.x - dragStartPoint.x;
    float deltaY = point.y - dragStartPoint.y;

    // Calculate new values based on drag mode
    switch (currentDragMode)
    {
        case DragMode::FrequencyAndGain:
        {
            // Frequency: logarithmic change
            float freqRatio = std::pow(maxFrequency / minFrequency, deltaX / displayBounds.getWidth());
            float newFreq = dragStartFreq * freqRatio;
            setBandFrequency(selectedBand, newFreq);

            // Gain: linear change (skip for HPF/LPF)
            if (selectedBand > 0 && selectedBand < 7)
            {
                float dbChange = -(deltaY / displayBounds.getHeight()) * (maxDisplayDB - minDisplayDB);
                setBandGain(selectedBand, dragStartGain + dbChange);
            }
            break;
        }

        case DragMode::GainOnly:
            if (selectedBand > 0 && selectedBand < 7)
            {
                float dbChange = -(deltaY / displayBounds.getHeight()) * (maxDisplayDB - minDisplayDB);
                setBandGain(selectedBand, dragStartGain + dbChange);
            }
            break;

        case DragMode::FrequencyOnly:
        {
            float freqRatio = std::pow(maxFrequency / minFrequency, deltaX / displayBounds.getWidth());
            float newFreq = dragStartFreq * freqRatio;
            setBandFrequency(selectedBand, newFreq);
            break;
        }

        case DragMode::QOnly:
        {
            // Q: exponential change based on vertical drag
            float qRatio = std::pow(2.0f, -deltaY / 50.0f);  // Double/half Q every 50 pixels
            float newQ = dragStartQ * qRatio;
            newQ = juce::jlimit(0.1f, 100.0f, newQ);
            setBandQ(selectedBand, newQ);
            break;
        }

        case DragMode::None:
            break;
    }

    repaint();
}

void EQGraphicDisplay::mouseUp(const juce::MouseEvent& /*e*/)
{
    isDragging = false;
    currentDragMode = DragMode::None;
}

void EQGraphicDisplay::mouseMove(const juce::MouseEvent& e)
{
    int hitBand = hitTestControlPoint(e.position);
    if (hitBand != hoveredBand)
    {
        hoveredBand = hitBand;
        repaint();
    }
}

void EQGraphicDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    // Double-click on control point resets band to default
    int hitBand = hitTestControlPoint(e.position);
    if (hitBand >= 0)
    {
        const auto& config = DefaultBandConfigs[static_cast<size_t>(hitBand)];
        setBandFrequency(hitBand, config.defaultFreq);
        if (hitBand > 0 && hitBand < 7)
            setBandGain(hitBand, 0.0f);
        setBandQ(hitBand, 0.71f);
        repaint();
    }
}

void EQGraphicDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Check if mouse is over a control point, or use selected band
    int targetBand = hitTestControlPoint(e.position);
    if (targetBand < 0)
        targetBand = selectedBand;

    if (targetBand >= 0 && isBandEnabled(targetBand))
    {
        float currentQ = getBandQ(targetBand);

        // Logarithmic Q adjustment - scroll up increases Q (narrower), scroll down decreases Q (wider)
        float multiplier = std::pow(1.15f, wheel.deltaY * 3.0f);
        float newQ = currentQ * multiplier;
        newQ = juce::jlimit(0.1f, 100.0f, newQ);

        setBandQ(targetBand, newQ);
        repaint();
    }
}

//==============================================================================
void EQGraphicDisplay::setSelectedBand(int bandIndex)
{
    selectedBand = bandIndex;
    repaint();
}

void EQGraphicDisplay::setDisplayScaleMode(DisplayScaleMode mode)
{
    scaleMode = mode;

    switch (mode)
    {
        case DisplayScaleMode::Linear12dB:
            minDisplayDB = -12.0f;
            maxDisplayDB = 12.0f;
            break;
        case DisplayScaleMode::Linear24dB:
            minDisplayDB = -24.0f;
            maxDisplayDB = 24.0f;
            break;
        case DisplayScaleMode::Linear30dB:
            minDisplayDB = -30.0f;
            maxDisplayDB = 30.0f;
            break;
        case DisplayScaleMode::Linear60dB:
            minDisplayDB = -60.0f;
            maxDisplayDB = 60.0f;
            break;
        case DisplayScaleMode::Warped:
            // Non-linear scale - more resolution around 0
            minDisplayDB = -24.0f;
            maxDisplayDB = 24.0f;
            break;
    }

    // Note: Analyzer uses its own fixed display range (-80 to 0 dB)
    // independent of the EQ display scale
    repaint();
}

void EQGraphicDisplay::setAnalyzerVisible(bool visible)
{
    analyzer->setVisible(visible);
    analyzer->setEnabled(visible);
}
//==============================================================================
juce::Rectangle<float> EQGraphicDisplay::getDisplayBounds() const
{
    return getLocalBounds().toFloat().reduced(40.0f, 20.0f);
}

float EQGraphicDisplay::getXForFrequency(float freq) const
{
    auto bounds = getDisplayBounds();
    float normalized = std::log(freq / minFrequency) / std::log(maxFrequency / minFrequency);
    normalized = juce::jlimit(0.0f, 1.0f, normalized);
    return bounds.getX() + normalized * bounds.getWidth();
}

float EQGraphicDisplay::getFrequencyAtX(float x) const
{
    auto bounds = getDisplayBounds();
    float normalized = (x - bounds.getX()) / bounds.getWidth();
    normalized = juce::jlimit(0.0f, 1.0f, normalized);
    return minFrequency * std::pow(maxFrequency / minFrequency, normalized);
}

float EQGraphicDisplay::getYForDB(float dB) const
{
    auto bounds = getDisplayBounds();

    if (scaleMode == DisplayScaleMode::Warped)
    {
        // Non-linear warped scale - more resolution around 0
        float sign = dB < 0 ? -1.0f : 1.0f;
        float absDB = std::abs(dB);
        float warped = sign * std::sqrt(absDB / std::abs(maxDisplayDB)) * std::abs(maxDisplayDB);
        float normalized = (warped - minDisplayDB) / (maxDisplayDB - minDisplayDB);
        normalized = juce::jlimit(0.0f, 1.0f, normalized);
        return bounds.getBottom() - normalized * bounds.getHeight();
    }
    else
    {
        float normalized = (dB - minDisplayDB) / (maxDisplayDB - minDisplayDB);
        normalized = juce::jlimit(0.0f, 1.0f, normalized);
        return bounds.getBottom() - normalized * bounds.getHeight();
    }
}

float EQGraphicDisplay::getDBAtY(float y) const
{
    auto bounds = getDisplayBounds();
    float normalized = (bounds.getBottom() - y) / bounds.getHeight();
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    if (scaleMode == DisplayScaleMode::Warped)
    {
        float value = minDisplayDB + normalized * (maxDisplayDB - minDisplayDB);
        float sign = value < 0 ? -1.0f : 1.0f;
        float absValue = std::abs(value);
        return sign * (absValue * absValue) / std::abs(maxDisplayDB);
    }
    else
    {
        return minDisplayDB + normalized * (maxDisplayDB - minDisplayDB);
    }
}

//==============================================================================
juce::Point<float> EQGraphicDisplay::getControlPointPosition(int bandIndex) const
{
    float freq = getBandFrequency(bandIndex);
    float gain = getBandGain(bandIndex);

    // For HPF/LPF, show at 0 dB
    if (bandIndex == 0 || bandIndex == 7)
        gain = 0.0f;

    return {getXForFrequency(freq), getYForDB(gain)};
}

int EQGraphicDisplay::hitTestControlPoint(juce::Point<float> point) const
{
    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
    {
        if (!isBandEnabled(i))
            continue;

        auto controlPoint = getControlPointPosition(i);
        float distance = point.getDistanceFrom(controlPoint);

        if (distance <= CONTROL_POINT_HIT_RADIUS)
            return i;
    }
    return -1;
}

//==============================================================================
float EQGraphicDisplay::getBandFrequency(int bandIndex) const
{
    auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandFreq(bandIndex + 1));
    return param ? param->load() : DefaultBandConfigs[static_cast<size_t>(bandIndex)].defaultFreq;
}

float EQGraphicDisplay::getBandGain(int bandIndex) const
{
    // HPF and LPF don't have gain
    if (bandIndex == 0 || bandIndex == 7)
        return 0.0f;

    auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandGain(bandIndex + 1));
    return param ? param->load() : 0.0f;
}

float EQGraphicDisplay::getBandQ(int bandIndex) const
{
    auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandQ(bandIndex + 1));
    return param ? param->load() : 0.71f;
}

bool EQGraphicDisplay::isBandEnabled(int bandIndex) const
{
    auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandEnabled(bandIndex + 1));
    return param ? param->load() > 0.5f : false;
}

void EQGraphicDisplay::setBandFrequency(int bandIndex, float freq)
{
    freq = juce::jlimit(minFrequency, maxFrequency, freq);
    if (auto* param = processor.parameters.getParameter(ParamIDs::bandFreq(bandIndex + 1)))
    {
        param->setValueNotifyingHost(param->convertTo0to1(freq));
    }
}

void EQGraphicDisplay::setBandGain(int bandIndex, float gain)
{
    if (bandIndex == 0 || bandIndex == 7)
        return;  // HPF/LPF don't have gain

    gain = juce::jlimit(-24.0f, 24.0f, gain);
    if (auto* param = processor.parameters.getParameter(ParamIDs::bandGain(bandIndex + 1)))
    {
        param->setValueNotifyingHost(param->convertTo0to1(gain));
    }
}

void EQGraphicDisplay::setBandQ(int bandIndex, float q)
{
    q = juce::jlimit(0.1f, 100.0f, q);
    if (auto* param = processor.parameters.getParameter(ParamIDs::bandQ(bandIndex + 1)))
    {
        param->setValueNotifyingHost(param->convertTo0to1(q));
    }
}

void EQGraphicDisplay::setBandEnabled(int bandIndex, bool enabled)
{
    if (auto* param = processor.parameters.getParameter(ParamIDs::bandEnabled(bandIndex + 1)))
    {
        param->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
    }

    if (onBandEnabledChanged)
        onBandEnabledChanged(bandIndex, enabled);
}

void EQGraphicDisplay::showBandContextMenu(int bandIndex, juce::Point<int> screenPos)
{
    const auto& config = DefaultBandConfigs[static_cast<size_t>(bandIndex)];
    bool isEnabled = isBandEnabled(bandIndex);

    juce::PopupMenu menu;

    // Band header (non-selectable)
    menu.addSectionHeader("Band " + juce::String(bandIndex + 1) + " - " +
                          juce::String(config.type == BandType::HighPass ? "High-Pass" :
                                       config.type == BandType::LowPass ? "Low-Pass" :
                                       config.type == BandType::LowShelf ? "Low Shelf" :
                                       config.type == BandType::HighShelf ? "High Shelf" : "Parametric"));

    menu.addSeparator();

    // Enable/Disable
    menu.addItem(1, isEnabled ? "Disable Band" : "Enable Band", true, false);

    // Reset to default
    menu.addItem(2, "Reset to Default", isEnabled);

    menu.addSeparator();

    // Solo band (disable all others temporarily - just visual hint)
    menu.addItem(3, "Solo This Band", isEnabled);

    // Enable all bands
    menu.addItem(4, "Enable All Bands");

    // Disable all bands
    menu.addItem(5, "Disable All Bands");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screenPos.x, screenPos.y, 1, 1}),
        [this, bandIndex, isEnabled](int result)
        {
            switch (result)
            {
                case 1:  // Toggle enable
                    setBandEnabled(bandIndex, !isEnabled);
                    repaint();
                    break;

                case 2:  // Reset to default
                {
                    const auto& cfg = DefaultBandConfigs[static_cast<size_t>(bandIndex)];
                    setBandFrequency(bandIndex, cfg.defaultFreq);
                    if (bandIndex > 0 && bandIndex < 7)
                        setBandGain(bandIndex, 0.0f);
                    setBandQ(bandIndex, 0.71f);
                    repaint();
                    break;
                }

                case 3:  // Solo - disable all others
                    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
                    {
                        if (i != bandIndex)
                            setBandEnabled(i, false);
                        else
                            setBandEnabled(i, true);
                    }
                    repaint();
                    break;

                case 4:  // Enable all
                    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
                        setBandEnabled(i, true);
                    repaint();
                    break;

                case 5:  // Disable all
                    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
                        setBandEnabled(i, false);
                    repaint();
                    break;

                default:
                    break;
            }
        });
}

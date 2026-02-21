#include "EQGraphicDisplay.h"
#include "MultiQ.h"

EQGraphicDisplay::EQGraphicDisplay(MultiQ& proc)
    : processor(proc)
{
    analyzer = std::make_unique<FFTAnalyzer>();
    addAndMakeVisible(analyzer.get());
    analyzer->setFillColor(juce::Colour(0x3055999a));   // ~19% fill (more subtle)
    analyzer->setLineColor(juce::Colour(0x6077aaaa));   // ~38% line (reduced)

    startTimerHz(60);
}

EQGraphicDisplay::~EQGraphicDisplay()
{
    stopTimer();
}

void EQGraphicDisplay::timerCallback()
{
    if (processor.isAnalyzerDataReady())
    {
        analyzer->updateMagnitudes(processor.getAnalyzerMagnitudes());
        processor.clearAnalyzerDataReady();
    }

    if (processor.isPreAnalyzerDataReady())
    {
        analyzer->updatePreMagnitudes(processor.getPreAnalyzerMagnitudes());
        processor.clearPreAnalyzerDataReady();
    }

    bool needsRepaint = false;

    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
    {
        float freq = getBandFrequency(i);
        float gain = getBandGain(i);
        float q = getBandQ(i);
        bool enabled = isBandEnabled(i);

        if (freq != lastBandFreqs[static_cast<size_t>(i)] ||
            gain != lastBandGains[static_cast<size_t>(i)] ||
            q != lastBandQs[static_cast<size_t>(i)] ||
            enabled != lastBandEnabled[static_cast<size_t>(i)])
        {
            lastBandFreqs[static_cast<size_t>(i)] = freq;
            lastBandGains[static_cast<size_t>(i)] = gain;
            lastBandQs[static_cast<size_t>(i)] = q;
            lastBandEnabled[static_cast<size_t>(i)] = enabled;
            needsRepaint = true;
        }

        if (processor.isInDynamicMode() && processor.isDynamicsEnabled(i))
        {
            float target = processor.getDynamicGain(i);
            constexpr float kSmoothCoeff = 0.85f;
            float prev = smoothedDynamicGains[static_cast<size_t>(i)];
            float smoothed = kSmoothCoeff * prev + (1.0f - kSmoothCoeff) * target;
            smoothedDynamicGains[static_cast<size_t>(i)] = smoothed;

            if (std::abs(smoothed - prev) > 0.1f)
                needsRepaint = true;
        }
        else
        {
            if (smoothedDynamicGains[static_cast<size_t>(i)] != 0.0f)
            {
                smoothedDynamicGains[static_cast<size_t>(i)] = 0.0f;
                needsRepaint = true;
            }
        }
    }

    if (needsRepaint || isDragging)
        repaint();
}

void EQGraphicDisplay::renderBackground()
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty()) return;

    backgroundCache = juce::Image(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
    juce::Graphics bg(backgroundCache);
    auto boundsF = bounds.toFloat();

    {
        auto centerX = boundsF.getCentreX();
        auto centerY = getYForDB(0.0f);

        juce::ColourGradient bgGradient(
            juce::Colour(0xFF1e1e20), centerX, centerY,
            juce::Colour(0xFF0a0a0c), 0.0f, 0.0f,
            true);
        bgGradient.addColour(0.25, juce::Colour(0xFF1a1a1c));
        bgGradient.addColour(0.5, juce::Colour(0xFF141416));
        bgGradient.addColour(0.75, juce::Colour(0xFF0f0f11));

        bg.setGradientFill(bgGradient);
        bg.fillRect(boundsF);
    }

    // Subtle vignette overlay for depth
    {
        juce::ColourGradient vignette(
            juce::Colours::transparentBlack, boundsF.getCentreX(), boundsF.getCentreY(),
            juce::Colour(0x30000000), boundsF.getX(), boundsF.getY(),
            true);
        bg.setGradientFill(vignette);
        bg.fillRect(boundsF);
    }

    drawGrid(bg);

    if (showPianoOverlay)
        drawPianoOverlay(bg);

    backgroundCacheDirty = false;
}

void EQGraphicDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (backgroundCacheDirty || !backgroundCache.isValid()
        || backgroundCache.getWidth() != getWidth()
        || backgroundCache.getHeight() != getHeight())
    {
        renderBackground();
    }
    g.drawImageAt(backgroundCache, 0, 0);

    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
    {
        if (isBandEnabled(i))
            drawBandCurve(g, i);
    }

    drawCombinedCurve(g);

    if (processor.isMatchMode() && processor.hasMatchOverlayData())
        drawMatchOverlays(g);

    if (processor.isInDynamicMode())
    {
        auto dynBounds = getDisplayBounds();
        juce::Path dynPath;
        std::vector<juce::Point<float>> staticPoints, dynPointsVec;
        int dynPoints = juce::jmax(100, static_cast<int>(dynBounds.getWidth() * 0.5f));

        for (int px = 0; px < dynPoints; ++px)
        {
            float x = dynBounds.getX() + static_cast<float>(px) * dynBounds.getWidth() / static_cast<float>(dynPoints);
            float freq = getFrequencyAtX(x);

            float staticResp = processor.getFrequencyResponseMagnitude(freq);
            float dynResp = processor.getFrequencyResponseWithDynamics(freq);

            float staticY = getYForDB(staticResp);
            float dynY = getYForDB(dynResp);

            staticPoints.push_back({x, staticY});
            dynPointsVec.push_back({x, dynY});

            if (px == 0)
                dynPath.startNewSubPath(x, dynY);
            else
                dynPath.lineTo(x, dynY);
        }

        // Shaded fill between static (combined) curve and dynamic curve
        if (!staticPoints.empty())
        {
            juce::Path fillRegion;
            fillRegion.startNewSubPath(dynPointsVec[0]);
            for (size_t i = 1; i < dynPointsVec.size(); ++i)
                fillRegion.lineTo(dynPointsVec[i]);
            for (int i = static_cast<int>(staticPoints.size()) - 1; i >= 0; --i)
                fillRegion.lineTo(staticPoints[static_cast<size_t>(i)]);
            fillRegion.closeSubPath();

            g.setColour(juce::Colour(0x22ffaa44));
            g.fillPath(fillRegion);
        }

        // Dynamic curve as solid orange line with glow
        g.setColour(juce::Colour(0x50ffaa44));
        g.strokePath(dynPath, juce::PathStrokeType(2.5f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0x90ffaa44));
        g.strokePath(dynPath, juce::PathStrokeType(1.2f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (showMasterGain && std::abs(masterGainDB) > 0.01f)
        drawMasterGainOverlay(g);

    if (selectedBand >= 0 && selectedBand < MultiQ::NUM_BANDS &&
        processor.isInDynamicMode() && processor.isDynamicsEnabled(selectedBand))
    {
        float threshold = processor.getDynamicsThreshold(selectedBand);
        float thresholdY = getYForDB(threshold);

        auto displayBounds = getDisplayBounds();

        juce::Colour threshColor = juce::Colour(0xFFff8844);  // Orange to match dynamics

        juce::Rectangle<float> compressionZone(
            displayBounds.getX(), displayBounds.getY(),
            displayBounds.getWidth(), thresholdY - displayBounds.getY());
        g.setColour(threshColor.withAlpha(0.05f));
        g.fillRect(compressionZone);

        g.setColour(threshColor.withAlpha(0.15f));
        g.drawHorizontalLine(static_cast<int>(thresholdY - 1), displayBounds.getX(), displayBounds.getRight());
        g.drawHorizontalLine(static_cast<int>(thresholdY + 1), displayBounds.getX(), displayBounds.getRight());

        g.setColour(threshColor.withAlpha(0.5f));
        g.drawHorizontalLine(static_cast<int>(thresholdY), displayBounds.getX(), displayBounds.getRight());

        g.setColour(threshColor);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        juce::String threshLabel = "T: " + juce::String(static_cast<int>(threshold)) + " dB";
        g.drawText(threshLabel, static_cast<int>(displayBounds.getRight() - 60),
                   static_cast<int>(thresholdY - 14), 55, 14, juce::Justification::centredRight);
    }

    drawControlPoints(g);

    {
        auto displayBounds = getDisplayBounds();
        int modeIndex = processor.getProcessingMode();
        if (modeIndex > 0)  // 0 = Stereo (no badge needed)
        {
            const char* modeLabels[] = { "", "LEFT", "RIGHT", "MID", "SIDE" };
            juce::String modeText = modeLabels[juce::jlimit(0, 4, modeIndex)];

            auto font = juce::FontOptions(11.0f, juce::Font::bold);
            g.setFont(font);
            float textWidth = g.getCurrentFont().getStringWidth(modeText) + 12.0f;
            float textHeight = 18.0f;
            float badgeX = displayBounds.getRight() - textWidth - 6.0f;
            float badgeY = displayBounds.getY() + 6.0f;

            // Background pill
            juce::Rectangle<float> badgeRect(badgeX, badgeY, textWidth, textHeight);
            g.setColour(juce::Colour(0xCC1a1a2e));
            g.fillRoundedRectangle(badgeRect, 4.0f);
            g.setColour(juce::Colour(0x60ffffff));
            g.drawRoundedRectangle(badgeRect, 4.0f, 1.0f);

            // Text
            g.setColour(juce::Colour(0xDDffffff));
            g.drawText(modeText, badgeRect, juce::Justification::centred);
        }
    }

    if (isSpectrumFrozen())
    {
        auto displayBounds = getDisplayBounds();
        juce::String frozenText = "FROZEN (F)";
        auto font = juce::FontOptions(11.0f, juce::Font::bold);
        g.setFont(font);
        float textWidth = g.getCurrentFont().getStringWidth(frozenText) + 12.0f;
        float textHeight = 18.0f;
        float badgeX = displayBounds.getX() + 6.0f;
        float badgeY = displayBounds.getY() + 6.0f;

        juce::Rectangle<float> badgeRect(badgeX, badgeY, textWidth, textHeight);
        g.setColour(juce::Colour(0xCC2e1a1a));
        g.fillRoundedRectangle(badgeRect, 4.0f);
        g.setColour(juce::Colour(0x6000ccff));
        g.drawRoundedRectangle(badgeRect, 4.0f, 1.0f);

        g.setColour(juce::Colour(0xDD00ccff));
        g.drawText(frozenText, badgeRect, juce::Justification::centred);
    }

    // Subtle inner shadow/border for depth
    {
        // Top shadow
        juce::ColourGradient topShadow(
            juce::Colour(0x20000000), 0, 0,
            juce::Colours::transparentBlack, 0, 8,
            false);
        g.setGradientFill(topShadow);
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 8.0f);

        // Bottom shadow
        juce::ColourGradient bottomShadow(
            juce::Colours::transparentBlack, 0, bounds.getBottom() - 8,
            juce::Colour(0x15000000), 0, bounds.getBottom(),
            false);
        g.setGradientFill(bottomShadow);
        g.fillRect(bounds.getX(), bounds.getBottom() - 8, bounds.getWidth(), 8.0f);
    }

    // Hover readout: frequency + dB at cursor position
    if (showHoverReadout && !isDragging)
    {
        auto displayBounds = getDisplayBounds();
        float hoverFreq = getFrequencyAtX(hoverPosition.x);
        float hoverDB = getDBAtY(hoverPosition.y);
        float eqResponse = processor.getFrequencyResponseMagnitude(hoverFreq);

        // Format frequency
        juce::String freqText;
        if (hoverFreq >= 1000.0f)
            freqText = juce::String(hoverFreq / 1000.0f, 2) + " kHz";
        else
            freqText = juce::String(static_cast<int>(hoverFreq)) + " Hz";

        // Format cursor dB and EQ response
        juce::String dbText = juce::String(hoverDB >= 0 ? "+" : "") + juce::String(hoverDB, 1) + " dB";
        juce::String eqText = juce::String("EQ: ") + (eqResponse >= 0 ? "+" : "") + juce::String(eqResponse, 1) + " dB";

        auto font = juce::FontOptions(10.0f).withStyle("Bold");
        g.setFont(font);
        float textW = juce::jmax(g.getCurrentFont().getStringWidth(freqText),
                                  g.getCurrentFont().getStringWidth(eqText)) + 14.0f;
        float textH = 42.0f;

        // Position tooltip near cursor, flip if near edges
        float tooltipX = hoverPosition.x + 14.0f;
        float tooltipY = hoverPosition.y - textH - 6.0f;
        if (tooltipX + textW > displayBounds.getRight())
            tooltipX = hoverPosition.x - textW - 6.0f;
        if (tooltipY < displayBounds.getY())
            tooltipY = hoverPosition.y + 14.0f;

        juce::Rectangle<float> tooltipRect(tooltipX, tooltipY, textW, textH);

        // Background pill
        g.setColour(juce::Colour(0xDD101014));
        g.fillRoundedRectangle(tooltipRect, 4.0f);
        g.setColour(juce::Colour(0x50ffffff));
        g.drawRoundedRectangle(tooltipRect, 4.0f, 0.75f);

        // Text lines
        g.setColour(juce::Colour(0xFFdddddd));
        g.drawText(freqText, tooltipRect.reduced(6, 2).removeFromTop(14.0f), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xFFaaaaaa));
        g.drawText(dbText, tooltipRect.reduced(6, 2).translated(0, 12.0f).removeFromTop(14.0f), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xFF88ccff));
        g.drawText(eqText, tooltipRect.reduced(6, 2).translated(0, 24.0f).removeFromTop(14.0f), juce::Justification::centredLeft);

        // Crosshair lines (subtle)
        g.setColour(juce::Colour(0x20ffffff));
        g.drawVerticalLine(static_cast<int>(hoverPosition.x), displayBounds.getY(), displayBounds.getBottom());
        g.drawHorizontalLine(static_cast<int>(hoverPosition.y), displayBounds.getX(), displayBounds.getRight());
    }

    // Subtle outer border
    g.setColour(juce::Colour(0xFF2a2a2e));
    g.drawRect(bounds, 1.0f);
}

void EQGraphicDisplay::resized()
{
    // Analyzer fills the entire display area
    analyzer->setBounds(getLocalBounds().reduced(40, 20));
    analyzer->setFrequencyRange(minFrequency, maxFrequency);
    analyzer->setDisplayRange(minDisplayDB, maxDisplayDB);
    backgroundCacheDirty = true;
}

void EQGraphicDisplay::drawGrid(juce::Graphics& g)
{
    auto displayBounds = getDisplayBounds();

    // Ultra-thin minor frequency grid lines (~8% opacity - very subtle)
    g.setColour(juce::Colour(0x14ffffff));  // ~8% white
    std::array<float, 7> minorFreqLines = {20.0f, 50.0f, 150.0f, 300.0f, 700.0f, 3000.0f, 7000.0f};
    for (float freq : minorFreqLines)
    {
        float x = getXForFrequency(freq);
        if (x >= displayBounds.getX() && x <= displayBounds.getRight())
        {
            juce::Line<float> line(x, displayBounds.getY(), x, displayBounds.getBottom());
            g.drawLine(line, 0.5f);
        }
    }

    // Thin major frequency grid lines (~12% opacity)
    g.setColour(juce::Colour(0x1Effffff));  // ~12% white
    std::array<float, 4> majorFreqLines = {100.0f, 1000.0f, 10000.0f, 20000.0f};
    for (float freq : majorFreqLines)
    {
        float x = getXForFrequency(freq);
        if (x >= displayBounds.getX() && x <= displayBounds.getRight())
        {
            juce::Line<float> line(x, displayBounds.getY(), x, displayBounds.getBottom());
            g.drawLine(line, 0.5f);
        }
    }

    float dbStep = 6.0f;
    if (scaleMode == DisplayScaleMode::Linear30dB) dbStep = 10.0f;
    if (scaleMode == DisplayScaleMode::Linear60dB) dbStep = 20.0f;

    for (float db = minDisplayDB; db <= maxDisplayDB; db += dbStep)
    {
        float y = getYForDB(db);
        if (std::abs(db) < 0.01f)  // 0 dB line - subtle emphasis
        {
            // Soft outer glow for 0dB line
            g.setColour(juce::Colour(0x0Cffffff));  // ~5%
            juce::Line<float> glowLine(displayBounds.getX(), y, displayBounds.getRight(), y);
            g.drawLine(glowLine, 2.5f);

            // Core 0dB line (~25% opacity - brighter than other lines)
            g.setColour(juce::Colour(0x40ffffff));  // ~25%
            g.drawLine(glowLine, 0.75f);
        }
        else
        {
            g.setColour(juce::Colour(0x1Affffff));  // ~10%
            juce::Line<float> line(displayBounds.getX(), y, displayBounds.getRight(), y);
            g.drawLine(line, 0.5f);
        }
    }

    juce::Font labelFont(juce::FontOptions(9.5f).withStyle("Regular"));
    g.setFont(labelFont);

    std::array<std::pair<float, const char*>, 4> majorLabels = {{
        {100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"}, {20000.0f, "20k"}
    }};

    for (auto& [freq, label] : majorLabels)
    {
        float x = getXForFrequency(freq);
        // Subtle text shadow for depth
        g.setColour(juce::Colour(0x30000000));
        g.drawText(label, static_cast<int>(x - 17), static_cast<int>(displayBounds.getBottom() + 4),
                   36, 14, juce::Justification::centred);
        // Main text
        g.setColour(juce::Colour(0xFF8a8a8a));
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
        g.setColour(juce::Colour(0xFF5a5a5a));
        g.drawText(label, static_cast<int>(x - 15), static_cast<int>(displayBounds.getBottom() + 3),
                   30, 14, juce::Justification::centred);
    }

    juce::Font dbFont(juce::FontOptions(9.0f).withStyle("Regular"));
    g.setFont(dbFont);

    for (float db = minDisplayDB; db <= maxDisplayDB; db += dbStep)
    {
        float y = getYForDB(db);
        juce::String label = (db > 0 ? "+" : "") + juce::String(static_cast<int>(db));

        // 0 dB label is brighter with subtle glow
        if (std::abs(db) < 0.01f)
        {
            g.setColour(juce::Colour(0xFF9a9a9a));
        }
        else
        {
            g.setColour(juce::Colour(0xFF707070));
        }

        g.drawText(label, 5, static_cast<int>(y - 7), 28, 14, juce::Justification::right);
    }
}

void EQGraphicDisplay::drawPianoOverlay(juce::Graphics& g)
{
    auto displayBounds = getDisplayBounds();

    // Piano strip at the very bottom of the display area
    float stripHeight = 16.0f;
    float stripY = displayBounds.getBottom() - stripHeight;

    // Semi-transparent background for the strip
    g.setColour(juce::Colour(0x20000000));
    g.fillRect(displayBounds.getX(), stripY, displayBounds.getWidth(), stripHeight);

    // Note frequencies (A4 = 440 Hz, equal temperament)
    // MIDI note 0 = C-1 = 8.176 Hz, each semitone = freq * 2^(1/12)
    // We draw from MIDI 24 (C1 ≈ 32.7 Hz) to MIDI 108 (C8 ≈ 4186 Hz)
    // Black key pattern: C# D# _ F# G# A# _ (relative to each octave)
    static const bool isBlackKey[12] = {
        false, true, false, true, false, false, true, false, true, false, true, false
    };
    static const char* noteNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    for (int midi = 24; midi <= 108; ++midi)
    {
        float freq = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
        if (freq < minFrequency || freq > maxFrequency)
            continue;

        float x = getXForFrequency(freq);
        if (x < displayBounds.getX() || x > displayBounds.getRight())
            continue;

        int noteInOctave = midi % 12;
        int octave = (midi / 12) - 1;
        bool isBlack = isBlackKey[noteInOctave];

        if (noteInOctave == 0)  // C notes - draw label and tick
        {
            // Tick mark
            g.setColour(juce::Colour(0x60ffffff));
            g.drawLine(x, stripY, x, stripY + stripHeight, 1.0f);

            // Label (e.g., "C4")
            g.setColour(juce::Colour(0xCC999999));
            g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            juce::String label = juce::String(noteNames[noteInOctave]) + juce::String(octave);
            g.drawText(label, static_cast<int>(x + 2), static_cast<int>(stripY + 1),
                       24, static_cast<int>(stripHeight - 2), juce::Justification::centredLeft);
        }
        else if (isBlack)
        {
            // Black key - small dark tick
            g.setColour(juce::Colour(0x20ffffff));
            g.drawLine(x, stripY + stripHeight * 0.5f, x, stripY + stripHeight, 0.5f);
        }
        else
        {
            // White key (non-C) - subtle tick
            g.setColour(juce::Colour(0x30ffffff));
            g.drawLine(x, stripY + stripHeight * 0.3f, x, stripY + stripHeight, 0.5f);
        }
    }

    // Thin separator line at top of piano strip
    g.setColour(juce::Colour(0x20ffffff));
    g.drawLine(displayBounds.getX(), stripY, displayBounds.getRight(), stripY, 0.5f);
}

void EQGraphicDisplay::drawBandCurve(juce::Graphics& g, int bandIndex)
{
    auto displayBounds = getDisplayBounds();
    juce::Colour curveColor = (bandIndex >= 0 && bandIndex < 8) ? DefaultBandConfigs[bandIndex].color : juce::Colours::white;

    juce::Path curvePath;
    bool pathStarted = false;

    int numPoints = juce::jmax(100, static_cast<int>(displayBounds.getWidth() * 0.75f));

    for (int px = 0; px < numPoints; ++px)
    {
        float x = displayBounds.getX() + static_cast<float>(px) * displayBounds.getWidth() / static_cast<float>(numPoints);
        float freq = getFrequencyAtX(x);

        float response = 0.0f;
        float bandFreq = getBandFrequency(bandIndex);
        float gain = getBandGain(bandIndex);
        float q = processor.getEffectiveQ(bandIndex + 1);  // 1-indexed

        static const float slopeValues[] = { 6.0f, 12.0f, 18.0f, 24.0f, 36.0f, 48.0f, 72.0f, 96.0f };

        if (bandIndex == 0)  // HPF
        {
            float ratio = freq / bandFreq;
            if (ratio < 1.0f)
            {
                auto slopeParam = processor.parameters.getRawParameterValue(ParamIDs::bandSlope(1));
                int slopeIndex = slopeParam ? static_cast<int>(slopeParam->load()) : 1;
                float slopeDB = (slopeIndex >= 0 && slopeIndex < 8) ? slopeValues[slopeIndex] : 12.0f;
                response = slopeDB * std::log2(ratio);
            }
        }
        else if (bandIndex == 7)  // LPF
        {
            float ratio = bandFreq / freq;
            if (ratio < 1.0f)
            {
                auto slopeParam = processor.parameters.getRawParameterValue(ParamIDs::bandSlope(8));
                int slopeIndex = slopeParam ? static_cast<int>(slopeParam->load()) : 1;
                float slopeDB = (slopeIndex >= 0 && slopeIndex < 8) ? slopeValues[slopeIndex] : 12.0f;
                response = slopeDB * std::log2(ratio);
            }
        }
        else if (bandIndex == 1)  // Band 2: shape-aware
        {
            auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(2));
            int shape = shapeParam ? static_cast<int>(shapeParam->load()) : 0;

            if (shape == 1)  // Peaking
            {
                float logRatio = std::log2(freq / bandFreq);
                float bandwidth = 1.0f / q;
                float envelope = std::exp(-logRatio * logRatio / (bandwidth * bandwidth * 0.5f));
                response = gain * envelope;
            }
            else if (shape == 2)  // High-Pass (12 dB/oct)
            {
                float ratio = freq / bandFreq;
                if (ratio < 1.0f)
                    response = 12.0f * std::log2(ratio);
            }
            else  // Low Shelf (default)
            {
                float ratio = freq / bandFreq;
                if (ratio < 0.5f)
                    response = gain;
                else if (ratio < 2.0f)
                {
                    float transition = (std::log2(ratio) + 1.0f) / 2.0f;
                    response = gain * (1.0f - transition);
                }
            }
        }
        else if (bandIndex == 6)  // Band 7: shape-aware
        {
            auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(7));
            int shape = shapeParam ? static_cast<int>(shapeParam->load()) : 0;

            if (shape == 1)  // Peaking
            {
                float logRatio = std::log2(freq / bandFreq);
                float bandwidth = 1.0f / q;
                float envelope = std::exp(-logRatio * logRatio / (bandwidth * bandwidth * 0.5f));
                response = gain * envelope;
            }
            else if (shape == 2)  // Low-Pass (12 dB/oct)
            {
                float ratio = bandFreq / freq;
                if (ratio < 1.0f)
                    response = 12.0f * std::log2(ratio);
            }
            else  // High Shelf (default)
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
        }
        else  // Parametric bands 3-6 (shape-aware)
        {
            auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(bandIndex + 1));
            int shape = shapeParam ? static_cast<int>(shapeParam->load()) : 0;

            if (shape == 3)  // Tilt Shelf
            {
                float tiltRatio = freq / bandFreq;
                float tiltTransition = 2.0f / juce::MathConstants<float>::pi * std::atan(std::log2(tiltRatio) * 2.0f);
                response = gain * tiltTransition;
            }
            else
            {
                float logRatio = std::log2(freq / bandFreq);
                float bandwidth = 1.0f / q;
                float envelope = std::exp(-logRatio * logRatio / (bandwidth * bandwidth * 0.5f));
                response = gain * envelope;
            }
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

    juce::Path fillPath = curvePath;
    float zeroY = getYForDB(0.0f);
    fillPath.lineTo(displayBounds.getRight(), zeroY);
    fillPath.lineTo(displayBounds.getX(), zeroY);
    fillPath.closeSubPath();

    bool isSelected = (bandIndex == selectedBand);
    bool isHovered = (bandIndex == hoveredBand);

    auto curveBounds = curvePath.getBounds();
    float peakY = (curveBounds.getY() < zeroY) ? curveBounds.getY() : curveBounds.getBottom();

    {
        juce::ColourGradient fillGradient;
        float curveAlpha = isSelected ? 0.35f : (isHovered ? 0.25f : 0.18f);

        if (peakY < zeroY)  // Boosting (curve above 0dB)
        {
            fillGradient = juce::ColourGradient(
                curveColor.withAlpha(curveAlpha), 0, peakY,
                curveColor.withAlpha(0.02f), 0, zeroY,
                false);
        }
        else  // Cutting (curve below 0dB)
        {
            fillGradient = juce::ColourGradient(
                curveColor.withAlpha(0.02f), 0, zeroY,
                curveColor.withAlpha(curveAlpha), 0, curveBounds.getBottom(),
                false);
        }

        g.setGradientFill(fillGradient);
        g.fillPath(fillPath);
    }

    float glowAlpha = isSelected ? 0.3f : (isHovered ? 0.2f : 0.12f);
    g.setColour(curveColor.withAlpha(glowAlpha));
    g.strokePath(curvePath, juce::PathStrokeType(isSelected ? 5.0f : 4.0f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    float lineWidth = isSelected ? 2.5f : (isHovered ? 2.0f : 1.8f);
    float lineAlpha = isSelected ? 1.0f : (isHovered ? 0.9f : 0.75f);
    g.setColour(curveColor.withAlpha(lineAlpha));
    g.strokePath(curvePath, juce::PathStrokeType(lineWidth,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EQGraphicDisplay::drawCombinedCurve(juce::Graphics& g)
{
    auto displayBounds = getDisplayBounds();

    juce::Path combinedPath;
    bool pathStarted = false;

    // Combined curve resolution
    int numPoints = juce::jmax(100, static_cast<int>(displayBounds.getWidth() * 0.5f));

    for (int px = 0; px < numPoints; ++px)
    {
        float x = displayBounds.getX() + static_cast<float>(px) * displayBounds.getWidth() / static_cast<float>(numPoints);
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

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.strokePath(combinedPath, juce::PathStrokeType(8.0f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.strokePath(combinedPath, juce::PathStrokeType(5.0f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.strokePath(combinedPath, juce::PathStrokeType(3.0f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.strokePath(combinedPath, juce::PathStrokeType(1.8f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EQGraphicDisplay::drawControlPoints(juce::Graphics& g)
{
    float zeroY = getYForDB(0.0f);

    // First draw stalks for all enabled bands (behind nodes)
    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
    {
        if (isBandEnabled(i))
        {
            auto point = getControlPointPosition(i);
            bool isSelected = (i == selectedBand);
            bool isHovered = (i == hoveredBand);

            juce::Colour stalkColor = (i >= 0 && i < 8) ? DefaultBandConfigs[i].color : juce::Colours::white;

            float stalkAlpha = isSelected ? 0.6f : (isHovered ? 0.4f : 0.25f);
            juce::ColourGradient stalkGradient(
                stalkColor.withAlpha(stalkAlpha), point.x, point.y,
                stalkColor.withAlpha(0.05f), point.x, zeroY,
                false);

            g.setGradientFill(stalkGradient);
            float stalkWidth = isSelected ? 2.5f : (isHovered ? 2.0f : 1.5f);
            g.drawLine(point.x, point.y, point.x, zeroY, stalkWidth);
        }
    }

    // Then draw inactive bands as faint indicators
    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
    {
        if (!isBandEnabled(i))
            drawInactiveBandIndicator(g, i);
    }

    // Finally draw active band nodes on top
    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
    {
        if (isBandEnabled(i))
            drawBandControlPoint(g, i);
    }
}

void EQGraphicDisplay::drawInactiveBandIndicator(juce::Graphics& g, int bandIndex)
{
    auto point = getControlPointPosition(bandIndex);
    juce::Colour color = (bandIndex >= 0 && bandIndex < 8) ? DefaultBandConfigs[bandIndex].color : juce::Colours::grey;

    float radius = CONTROL_POINT_RADIUS * 0.7f;
    float ringThickness = 1.5f;
    float innerRadius = radius - ringThickness;

    // Faint outer ring
    g.setColour(color.withAlpha(0.2f));
    g.drawEllipse(point.x - radius, point.y - radius, radius * 2.0f, radius * 2.0f, ringThickness);

    // Very faint center fill
    g.setColour(color.withAlpha(0.08f));
    g.fillEllipse(point.x - innerRadius, point.y - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);

    // Faint band number
    g.setColour(color.withAlpha(0.35f));
    g.setFont(juce::Font(juce::FontOptions(8.0f).withStyle("Bold")));
    g.drawText(juce::String(bandIndex + 1),
               static_cast<int>(point.x - radius), static_cast<int>(point.y - radius),
               static_cast<int>(radius * 2.0f), static_cast<int>(radius * 2.0f),
               juce::Justification::centred);
}

void EQGraphicDisplay::drawBandControlPoint(juce::Graphics& g, int bandIndex)
{
    auto point = getControlPointPosition(bandIndex);
    juce::Colour color = (bandIndex >= 0 && bandIndex < 8) ? DefaultBandConfigs[bandIndex].color : juce::Colours::white;

    bool isSelected = (bandIndex == selectedBand);
    bool isHovered = (bandIndex == hoveredBand);

    float gain = getBandGain(bandIndex);
    bool isFlat = (bandIndex > 0 && bandIndex < 7) && std::abs(gain) < 0.5f;  // Within 0.5dB of 0
    bool hasGain = !isFlat;

    float baseRadius = CONTROL_POINT_RADIUS;
    float flatScale = isFlat ? 0.85f : 1.0f;  // Flat nodes are slightly smaller
    float scale = (isSelected ? 1.25f : (isHovered ? 1.15f : 1.0f)) * flatScale;
    float radius = baseRadius * scale;

    float opacityMult = (isFlat && !isSelected && !isHovered) ? 0.6f : 1.0f;

    float ringThickness = isSelected ? 3.0f : (isHovered ? 2.5f : (isFlat ? 1.5f : 2.0f));
    float innerRadius = radius - ringThickness;

    if (processor.isInDynamicMode() && processor.isDynamicsEnabled(bandIndex))
    {
        float dynGain = smoothedDynamicGains[static_cast<size_t>(bandIndex)];
        if (std::abs(dynGain) > 0.5f)
        {
            auto staticPoint = getStaticControlPointPosition(bandIndex);
            float ghostRadius = baseRadius * 0.7f;

            // Faint outline ring at static position
            g.setColour(color.withAlpha(0.25f));
            g.drawEllipse(staticPoint.x - ghostRadius, staticPoint.y - ghostRadius,
                          ghostRadius * 2.0f, ghostRadius * 2.0f, 1.5f);

            g.setColour(color.withAlpha(0.15f));
            g.drawLine(staticPoint.x, staticPoint.y, point.x, point.y, 1.0f);
        }
    }

    if (isSelected)
    {
        g.setColour(color.withAlpha(0.15f));
        g.fillEllipse(point.x - radius * 2.2f, point.y - radius * 2.2f,
                      radius * 4.4f, radius * 4.4f);
        g.setColour(color.withAlpha(0.25f));
        g.fillEllipse(point.x - radius * 1.7f, point.y - radius * 1.7f,
                      radius * 3.4f, radius * 3.4f);
        g.setColour(color.withAlpha(0.4f));
        g.fillEllipse(point.x - radius * 1.3f, point.y - radius * 1.3f,
                      radius * 2.6f, radius * 2.6f);
    }
    else if (isHovered)
    {
        g.setColour(color.withAlpha(0.12f));
        g.fillEllipse(point.x - radius * 1.8f, point.y - radius * 1.8f,
                      radius * 3.6f, radius * 3.6f);
        g.setColour(color.withAlpha(0.2f));
        g.fillEllipse(point.x - radius * 1.4f, point.y - radius * 1.4f,
                      radius * 2.8f, radius * 2.8f);
    }
    else if (hasGain)
    {
        g.setColour(color.withAlpha(0.08f));
        g.fillEllipse(point.x - radius * 1.5f, point.y - radius * 1.5f,
                      radius * 3.0f, radius * 3.0f);
    }

    g.setColour(juce::Colour(0x40000000).withMultipliedAlpha(opacityMult));
    g.fillEllipse(point.x - radius + 2.0f, point.y - radius + 2.0f, radius * 2.0f, radius * 2.0f);

    g.setColour(color.withMultipliedAlpha(opacityMult));
    g.fillEllipse(point.x - radius, point.y - radius, radius * 2.0f, radius * 2.0f);

    juce::Colour centerColor = isSelected ? juce::Colour(0xE0101014) : juce::Colour(0xD0141418);
    g.setColour(centerColor);
    g.fillEllipse(point.x - innerRadius, point.y - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);

    if (!isFlat || isSelected || isHovered)
    {
        g.setColour(color.brighter(0.3f).withAlpha(0.4f * opacityMult));
        g.drawEllipse(point.x - innerRadius + 0.5f, point.y - innerRadius + 0.5f,
                      (innerRadius - 0.5f) * 2.0f, (innerRadius - 0.5f) * 2.0f, 0.75f);
    }

    if (isSelected)
    {
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawEllipse(point.x - radius - 0.5f, point.y - radius - 0.5f,
                      (radius + 0.5f) * 2.0f, (radius + 0.5f) * 2.0f, 1.5f);
    }

    BandType bandType = (bandIndex >= 0 && bandIndex < 8)
        ? DefaultBandConfigs[static_cast<size_t>(bandIndex)].type
        : BandType::Parametric;

    if (bandType == BandType::Parametric && bandIndex >= 2 && bandIndex <= 5)
    {
        auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(bandIndex + 1));
        if (shapeParam)
        {
            int shape = static_cast<int>(shapeParam->load());
            if (shape == 1) bandType = BandType::Notch;
            else if (shape == 2) bandType = BandType::BandPass;
        }
    }

    g.setColour(juce::Colours::white.withAlpha((isSelected ? 1.0f : 0.9f) * opacityMult));

    float iconSize = innerRadius * 1.1f;
    float strokeWidth = isSelected ? 2.0f : 1.5f;

    switch (bandType)
    {
        case BandType::HighPass:
        {
            // HPF icon: slope rising to the right (/¯)
            juce::Path hpfPath;
            float cx = point.x, cy = point.y;
            hpfPath.startNewSubPath(cx - iconSize * 0.6f, cy + iconSize * 0.4f);
            hpfPath.lineTo(cx - iconSize * 0.1f, cy + iconSize * 0.4f);
            hpfPath.lineTo(cx + iconSize * 0.3f, cy - iconSize * 0.4f);
            hpfPath.lineTo(cx + iconSize * 0.6f, cy - iconSize * 0.4f);
            g.strokePath(hpfPath, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            break;
        }
        case BandType::LowPass:
        {
            // LPF icon: slope falling to the right (¯\)
            juce::Path lpfPath;
            float cx = point.x, cy = point.y;
            lpfPath.startNewSubPath(cx - iconSize * 0.6f, cy - iconSize * 0.4f);
            lpfPath.lineTo(cx - iconSize * 0.3f, cy - iconSize * 0.4f);
            lpfPath.lineTo(cx + iconSize * 0.1f, cy + iconSize * 0.4f);
            lpfPath.lineTo(cx + iconSize * 0.6f, cy + iconSize * 0.4f);
            g.strokePath(lpfPath, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            break;
        }
        case BandType::LowShelf:
        {
            // Low shelf icon: step up shape
            juce::Path shelfPath;
            float cx = point.x, cy = point.y;
            shelfPath.startNewSubPath(cx - iconSize * 0.6f, cy + iconSize * 0.3f);
            shelfPath.lineTo(cx - iconSize * 0.15f, cy + iconSize * 0.3f);
            shelfPath.lineTo(cx + iconSize * 0.15f, cy - iconSize * 0.3f);
            shelfPath.lineTo(cx + iconSize * 0.6f, cy - iconSize * 0.3f);
            g.strokePath(shelfPath, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            break;
        }
        case BandType::HighShelf:
        {
            // High shelf icon: step down shape
            juce::Path shelfPath;
            float cx = point.x, cy = point.y;
            shelfPath.startNewSubPath(cx - iconSize * 0.6f, cy - iconSize * 0.3f);
            shelfPath.lineTo(cx - iconSize * 0.15f, cy - iconSize * 0.3f);
            shelfPath.lineTo(cx + iconSize * 0.15f, cy + iconSize * 0.3f);
            shelfPath.lineTo(cx + iconSize * 0.6f, cy + iconSize * 0.3f);
            g.strokePath(shelfPath, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            break;
        }
        case BandType::Notch:
        {
            // Notch icon: V-shaped dip (narrow rejection)
            juce::Path notchPath;
            float cx = point.x, cy = point.y;
            notchPath.startNewSubPath(cx - iconSize * 0.6f, cy - iconSize * 0.3f);
            notchPath.lineTo(cx - iconSize * 0.15f, cy - iconSize * 0.3f);
            notchPath.lineTo(cx, cy + iconSize * 0.5f);
            notchPath.lineTo(cx + iconSize * 0.15f, cy - iconSize * 0.3f);
            notchPath.lineTo(cx + iconSize * 0.6f, cy - iconSize * 0.3f);
            g.strokePath(notchPath, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            break;
        }
        case BandType::BandPass:
        {
            // BandPass icon: inverted V / peak shape
            juce::Path bpPath;
            float cx = point.x, cy = point.y;
            bpPath.startNewSubPath(cx - iconSize * 0.6f, cy + iconSize * 0.3f);
            bpPath.lineTo(cx - iconSize * 0.15f, cy + iconSize * 0.3f);
            bpPath.lineTo(cx, cy - iconSize * 0.5f);
            bpPath.lineTo(cx + iconSize * 0.15f, cy + iconSize * 0.3f);
            bpPath.lineTo(cx + iconSize * 0.6f, cy + iconSize * 0.3f);
            g.strokePath(bpPath, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            break;
        }
        case BandType::Parametric:
        default:
        {
            // Parametric/other: show band number
            float fontSize = isSelected ? 10.0f : (isFlat ? 8.0f : 9.0f);
            g.setFont(juce::Font(juce::FontOptions(fontSize).withStyle("Bold")));
            g.drawText(juce::String(bandIndex + 1),
                       static_cast<int>(point.x - innerRadius), static_cast<int>(point.y - innerRadius),
                       static_cast<int>(innerRadius * 2.0f), static_cast<int>(innerRadius * 2.0f),
                       juce::Justification::centred);
            break;
        }
    }

    if (processor.isInDynamicMode() && processor.isDynamicsEnabled(bandIndex))
    {
        float dynGain = processor.getDynamicGain(bandIndex);  // Negative dB for reduction

        if (std::abs(dynGain) > 0.5f)  // Only show if significant activity
        {
            float normalizedGain = juce::jmin(std::abs(dynGain) / 24.0f, 1.0f);

            float arcRadius = radius + 4.0f;
            float arcThickness = 2.5f;

            juce::Colour arcColor = juce::Colour(0xff00cc66).interpolatedWith(
                juce::Colour(0xffffcc00), normalizedGain * 0.7f);

            float startAngle = -juce::MathConstants<float>::halfPi;  // Top
            float endAngle = startAngle + normalizedGain * juce::MathConstants<float>::twoPi * 0.8f;

            juce::Path arcPath;
            arcPath.addCentredArc(point.x, point.y, arcRadius, arcRadius,
                                  0.0f, startAngle, endAngle, true);

            g.setColour(arcColor.withAlpha(0.9f));
            g.strokePath(arcPath, juce::PathStrokeType(arcThickness,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(arcColor.withAlpha(0.3f));
            g.strokePath(arcPath, juce::PathStrokeType(arcThickness + 2.0f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
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

    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawHorizontalLine(static_cast<int>(y), displayBounds.getX(), displayBounds.getRight());
}

void EQGraphicDisplay::drawMatchOverlays(juce::Graphics& g)
{
    auto displayBounds = getDisplayBounds();
    int numPoints = juce::jmax(200, static_cast<int>(displayBounds.getWidth()));

    const auto& refMags = processor.getMatchReferenceMagnitudes();
    const auto& diffCurve = processor.getMatchDifferenceCurve();
    float nyquist = static_cast<float>(processor.getBaseSampleRate() * 0.5);
    if (nyquist < 1.0f) nyquist = 22050.0f;

    // --- Reference spectrum overlay (green filled area) ---
    juce::Path refPath;
    juce::Path refFill;
    bool started = false;

    for (int px = 0; px < numPoints; ++px)
    {
        float x = displayBounds.getX() + static_cast<float>(px) * displayBounds.getWidth() / static_cast<float>(numPoints);
        float freq = getFrequencyAtX(x);

        int bin = static_cast<int>(freq / nyquist * static_cast<float>(EQMatchProcessor::NUM_BINS));
        bin = juce::jlimit(0, EQMatchProcessor::NUM_BINS - 1, bin);
        float refDB = refMags[static_cast<size_t>(bin)];
        float yPos = getYForDB(refDB);

        if (!started)
        {
            refPath.startNewSubPath(x, yPos);
            refFill.startNewSubPath(x, displayBounds.getBottom());
            refFill.lineTo(x, yPos);
            started = true;
        }
        else
        {
            refPath.lineTo(x, yPos);
            refFill.lineTo(x, yPos);
        }
    }

    float lastX = displayBounds.getX() + displayBounds.getWidth();
    refFill.lineTo(lastX, displayBounds.getBottom());
    refFill.closeSubPath();

    g.setColour(juce::Colour(0x1844cc88));
    g.fillPath(refFill);
    g.setColour(juce::Colour(0x6044cc88));
    g.strokePath(refPath, juce::PathStrokeType(1.5f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // --- Difference curve overlay (orange/amber line) ---
    juce::Path diffPath;
    started = false;

    for (int px = 0; px < numPoints; ++px)
    {
        float x = displayBounds.getX() + static_cast<float>(px) * displayBounds.getWidth() / static_cast<float>(numPoints);
        float freq = getFrequencyAtX(x);

        int bin = static_cast<int>(freq / nyquist * static_cast<float>(EQMatchProcessor::NUM_BINS));
        bin = juce::jlimit(0, EQMatchProcessor::NUM_BINS - 1, bin);
        float diffDB = diffCurve[static_cast<size_t>(bin)];
        float yPos = getYForDB(diffDB);

        if (!started)
        {
            diffPath.startNewSubPath(x, yPos);
            started = true;
        }
        else
        {
            diffPath.lineTo(x, yPos);
        }
    }

    g.setColour(juce::Colour(0x50ffaa44));
    g.strokePath(diffPath, juce::PathStrokeType(2.5f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0x90ffaa44));
    g.strokePath(diffPath, juce::PathStrokeType(1.2f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EQGraphicDisplay::mouseDown(const juce::MouseEvent& e)
{
    auto point = e.position;

    int hitBand = hitTestControlPoint(point);

    if (hitBand < 0)
    {
        for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
        {
            if (!isBandEnabled(i))
            {
                auto controlPoint = getControlPointPosition(i);
                float distance = point.getDistanceFrom(controlPoint);
                if (distance <= getHitRadius() * 1.2f)  // Slightly larger hit area for inactive
                {
                    hitBand = i;
                    break;
                }
            }
        }
    }

    if (e.mods.isRightButtonDown() && hitBand >= 0)
    {
        selectedBand = hitBand;
        if (onBandSelected)
            onBandSelected(selectedBand);
        repaint();
        showBandContextMenu(hitBand, e.getScreenPosition());
        return;
    }

    if (e.mods.isAltDown() && !e.mods.isCommandDown() && hitBand >= 0 && isBandEnabled(hitBand))
    {
        if (hitBand >= 8)
            return;  // Out of range for DefaultBandConfigs
        const auto& config = DefaultBandConfigs[static_cast<size_t>(hitBand)];
        setBandFrequency(hitBand, config.defaultFreq);
        setBandGain(hitBand, 0.0f);  // Default gain is 0 dB
        setBandQ(hitBand, 0.71f);    // Default Q (Butterworth)

        selectedBand = hitBand;
        if (onBandSelected)
            onBandSelected(selectedBand);
        repaint();
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

    switch (currentDragMode)
    {
        case DragMode::FrequencyAndGain:
        {
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
    bool changed = false;

    if (hitBand != hoveredBand)
    {
        hoveredBand = hitBand;
        changed = true;
    }

    auto displayBounds = getDisplayBounds();
    bool inDisplay = displayBounds.contains(e.position);
    if (inDisplay != showHoverReadout || (inDisplay && e.position != hoverPosition))
    {
        showHoverReadout = inDisplay;
        hoverPosition = e.position;
        changed = true;
    }

    if (changed)
        repaint();
}

void EQGraphicDisplay::mouseExit(const juce::MouseEvent& /*e*/)
{
    if (showHoverReadout || hoveredBand >= 0)
    {
        showHoverReadout = false;
        hoveredBand = -1;
        repaint();
    }
}

void EQGraphicDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    // Double-click on control point resets band to default
    int hitBand = hitTestControlPoint(e.position);
    if (hitBand >= 0 && hitBand < 8)
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

    if (analyzer)
        analyzer->setDisplayRange(minDisplayDB, maxDisplayDB);

    backgroundCacheDirty = true;
    repaint();
}

void EQGraphicDisplay::setAnalyzerVisible(bool visible)
{
    if (analyzer)
    {
        analyzer->setVisible(visible);
        analyzer->setEnabled(visible);
    }
}
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

juce::Point<float> EQGraphicDisplay::getStaticControlPointPosition(int bandIndex) const
{
    float freq = getBandFrequency(bandIndex);
    float gain = getBandGain(bandIndex);

    // For HPF/LPF, show at 0 dB
    if (bandIndex == 0 || bandIndex == 7)
        gain = 0.0f;

    if (bandIndex >= 2 && bandIndex <= 5)
    {
        auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(bandIndex + 1));
        if (shapeParam && static_cast<int>(shapeParam->load()) != 0)
            gain = 0.0f;
    }

    return {getXForFrequency(freq), getYForDB(gain)};
}

juce::Point<float> EQGraphicDisplay::getControlPointPosition(int bandIndex) const
{
    float freq = getBandFrequency(bandIndex);
    float gain = getBandGain(bandIndex);

    // For HPF/LPF, show at 0 dB
    if (bandIndex == 0 || bandIndex == 7)
        gain = 0.0f;

    if (bandIndex >= 2 && bandIndex <= 5)
    {
        auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(bandIndex + 1));
        if (shapeParam && static_cast<int>(shapeParam->load()) != 0)
            gain = 0.0f;
    }

    if (processor.isInDynamicMode() && processor.isDynamicsEnabled(bandIndex))
        gain += smoothedDynamicGains[static_cast<size_t>(bandIndex)];

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

        if (distance <= getHitRadius())
            return i;
    }
    return -1;
}

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

    // Notch/BandPass shapes don't have gain (shapes 1 and 2)
    if (bandIndex >= 2 && bandIndex <= 5)
    {
        auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(bandIndex + 1));
        int shape = shapeParam ? static_cast<int>(shapeParam->load()) : 0;
        if (shape == 1 || shape == 2)  // Notch or BandPass
            return;
    }

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
    if (bandIndex < 0 || bandIndex >= 8)
        return;

    const auto& config = DefaultBandConfigs[static_cast<size_t>(bandIndex)];
    bool isEnabled = isBandEnabled(bandIndex);

    juce::PopupMenu menu;

    // Band header (non-selectable) - check current shape
    juce::String bandTypeName;
    if (config.type == BandType::HighPass) bandTypeName = "High-Pass";
    else if (config.type == BandType::LowPass) bandTypeName = "Low-Pass";
    else if (bandIndex >= 1 && bandIndex <= 6)
    {
        auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(bandIndex + 1));
        int shape = shapeParam ? static_cast<int>(shapeParam->load()) : 0;

        if (bandIndex == 1)
        {
            const char* names[] = { "Low Shelf", "Peaking", "High-Pass" };
            bandTypeName = names[juce::jlimit(0, 2, shape)];
        }
        else if (bandIndex == 6)
        {
            const char* names[] = { "High Shelf", "Peaking", "Low-Pass" };
            bandTypeName = names[juce::jlimit(0, 2, shape)];
        }
        else
        {
            const char* names[] = { "Parametric", "Notch", "Band Pass", "Tilt Shelf" };
            bandTypeName = names[juce::jlimit(0, 3, shape)];
        }
    }
    else
    {
        bandTypeName = config.name;
    }
    menu.addSectionHeader("Band " + juce::String(bandIndex + 1) + " - " + bandTypeName);

    menu.addSeparator();

    menu.addItem(1, isEnabled ? "Disable Band" : "Enable Band", true, false);

    menu.addItem(2, "Reset to Default", isEnabled);

    menu.addSeparator();

    menu.addItem(3, "Solo This Band", isEnabled);

    bool isDelta = processor.isDeltaSoloMode() && processor.isBandSoloed(bandIndex);
    menu.addItem(8, "Delta Solo (Listen)", isEnabled, isDelta);

    menu.addItem(4, "Enable All Bands");

    menu.addItem(5, "Disable All Bands");

    menu.addSeparator();

    bool preVisible = analyzer ? analyzer->isPreSpectrumVisible() : false;
    menu.addItem(9, "Show Pre-EQ Spectrum", true, preVisible);

    menu.addSeparator();

    // Undo/Redo
    menu.addItem(6, "Undo", processor.getUndoManager().canUndo());
    menu.addItem(7, "Redo", processor.getUndoManager().canRedo());

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screenPos.x, screenPos.y, 1, 1}),
        [safeThis = juce::Component::SafePointer<EQGraphicDisplay>(this), bandIndex, isEnabled](int result)
        {
            if (safeThis == nullptr)
                return;
            switch (result)
            {
                case 1:  // Toggle enable
                    safeThis->setBandEnabled(bandIndex, !isEnabled);
                    safeThis->repaint();
                    break;

                case 2:  // Reset to default
                {
                    const auto& cfg = DefaultBandConfigs[static_cast<size_t>(bandIndex)];
                    safeThis->setBandFrequency(bandIndex, cfg.defaultFreq);
                    if (bandIndex > 0 && bandIndex < 7)
                        safeThis->setBandGain(bandIndex, 0.0f);
                    safeThis->setBandQ(bandIndex, 0.71f);
                    safeThis->repaint();
                    break;
                }

                case 3:  // Solo - disable all others
                    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
                    {
                        if (i != bandIndex)
                            safeThis->setBandEnabled(i, false);
                        else
                            safeThis->setBandEnabled(i, true);
                    }
                    safeThis->repaint();
                    break;

                case 4:  // Enable all
                    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
                        safeThis->setBandEnabled(i, true);
                    safeThis->repaint();
                    break;

                case 5:  // Disable all
                    for (int i = 0; i < MultiQ::NUM_BANDS; ++i)
                        safeThis->setBandEnabled(i, false);
                    safeThis->repaint();
                    break;

                case 6:  // Undo
                    safeThis->processor.getUndoManager().undo();
                    safeThis->repaint();
                    break;

                case 7:  // Redo
                    safeThis->processor.getUndoManager().redo();
                    safeThis->repaint();
                    break;

                case 8:  // Delta solo toggle
                {
                    bool wasActive = safeThis->processor.isDeltaSoloMode() && safeThis->processor.isBandSoloed(bandIndex);
                    if (wasActive)
                    {
                        // Turn off delta solo
                        safeThis->processor.setDeltaSoloMode(false);
                        safeThis->processor.setSoloedBand(-1);
                    }
                    else
                    {
                        // Activate delta solo for this band
                        safeThis->processor.setSoloedBand(bandIndex);
                        safeThis->processor.setDeltaSoloMode(true);
                    }
                    safeThis->repaint();
                    break;
                }

                case 9:  // Toggle pre-EQ spectrum overlay
                    if (safeThis->analyzer)
                        safeThis->analyzer->setShowPreSpectrum(!safeThis->analyzer->isPreSpectrumVisible());
                    break;

                default:
                    break;
            }
        });
}

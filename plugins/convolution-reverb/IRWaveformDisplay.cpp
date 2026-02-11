/*
  ==============================================================================

    Convolution Reverb - IR Waveform Display
    Waveform visualization with envelope overlay
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#include "IRWaveformDisplay.h"

IRWaveformDisplay::IRWaveformDisplay()
{
    startTimerHz(30);
}

IRWaveformDisplay::~IRWaveformDisplay()
{
    stopTimer();
}

void IRWaveformDisplay::setIRWaveform(const juce::AudioBuffer<float>& ir, double sampleRate)
{
    irBuffer.makeCopyOf(ir);
    irSampleRate = sampleRate;
    hasWaveform = irBuffer.getNumSamples() > 0;
    needsRepaint = true;
    rebuildWaveformPath();
    rebuildEnvelopePath();
}

void IRWaveformDisplay::clearWaveform()
{
    irBuffer.setSize(0, 0);
    waveformPath.clear();
    envelopePath.clear();
    hasWaveform = false;
    needsRepaint = true;
}
void IRWaveformDisplay::setEnvelopeParameters(float attack, float decay, float length)
{
    if (std::abs(attackParam - attack) > 0.001f ||
        std::abs(decayParam - decay) > 0.001f ||
        std::abs(lengthParam - length) > 0.001f)
    {
        attackParam = attack;
        decayParam = decay;
        lengthParam = length;
        needsRepaint = true;
        rebuildEnvelopePath();
    }
}

void IRWaveformDisplay::setIROffset(float offset)
{
    if (std::abs(irOffsetParam - offset) > 0.001f)
    {
        irOffsetParam = offset;
        needsRepaint = true;
    }
}

void IRWaveformDisplay::setFilterEnvelope(bool enabled, float initFreq, float endFreq, float attack)
{
    // Validate frequency parameters for log calculations
    initFreq = std::max(initFreq, 1.0f);
    endFreq = std::max(endFreq, 1.0f);
    
    if (filterEnvEnabled != enabled ||
        std::abs(filterEnvInitFreq - initFreq) > 1.0f ||
        std::abs(filterEnvEndFreq - endFreq) > 1.0f ||
        std::abs(filterEnvAttack - attack) > 0.001f)
    {
        filterEnvEnabled = enabled;
        filterEnvInitFreq = initFreq;
        filterEnvEndFreq = endFreq;
        filterEnvAttack = attack;
        needsRepaint = true;
    }
}
void IRWaveformDisplay::setReversed(bool isReversed)
{
    if (reversed != isReversed)
    {
        reversed = isReversed;
        needsRepaint = true;
        rebuildWaveformPath();
    }
}

void IRWaveformDisplay::setPlaybackPosition(float position)
{
    playbackPosition = juce::jlimit(0.0f, 1.0f, position);
    needsRepaint = true;
}
void IRWaveformDisplay::timerCallback()
{
    if (needsRepaint)
    {
        repaint();
        needsRepaint = false;
    }
}

void IRWaveformDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(backgroundColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Border
    g.setColour(gridColour);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Draw mode toggle (always visible)
    drawModeToggle(g);

    // If EQ mode, draw EQ curve regardless of IR state
    if (displayMode == DisplayMode::EQCurve)
    {
        auto contentBounds = bounds.reduced(10, 25);
        contentBounds.removeFromTop(5);  // Space below toggle
        drawEQCurve(g, contentBounds);
        return;
    }

    // IR Waveform mode
    if (!hasWaveform)
    {
        // No IR loaded message
        g.setColour(textColour);
        g.setFont(juce::Font(14.0f));
        g.drawText("No IR Loaded", bounds.withTrimmedTop(30), juce::Justification::centred);
        g.setFont(juce::Font(11.0f));
        g.drawText("Select an impulse response from the browser",
                   bounds.withTrimmedTop(30).translated(0, 20), juce::Justification::centred);
        return;
    }

    auto waveformBounds = bounds.reduced(10, 25);
    waveformBounds.removeFromTop(5);  // Space below toggle

    // Draw time grid
    drawTimeGrid(g);

    // Draw center line
    g.setColour(gridColour.brighter(0.2f));
    g.drawHorizontalLine(static_cast<int>(waveformBounds.getCentreY()),
                         waveformBounds.getX(), waveformBounds.getRight());

    // Draw waveform
    g.setColour(waveformColour);
    g.strokePath(waveformPath, juce::PathStrokeType(1.0f));

    // Fill waveform with gradient
    juce::ColourGradient waveGrad(
        waveformColour.withAlpha(0.4f), waveformBounds.getCentreX(), waveformBounds.getY(),
        waveformColour.withAlpha(0.1f), waveformBounds.getCentreX(), waveformBounds.getBottom(),
        false
    );
    g.setGradientFill(waveGrad);

    juce::Path filledWaveform = waveformPath;
    filledWaveform.lineTo(waveformBounds.getRight(), waveformBounds.getCentreY());
    filledWaveform.lineTo(waveformBounds.getX(), waveformBounds.getCentreY());
    filledWaveform.closeSubPath();
    g.fillPath(filledWaveform);

    // Draw enhanced envelope overlay with semi-transparent fill, anti-aliased paths
    if (!envelopePath.isEmpty())
    {
        // Create a mirrored envelope path for the filled shape
        juce::Path envelopeFill;
        auto envBounds = waveformBounds;

        // Build the full envelope shape (top and bottom mirrored)
        EnvelopeProcessor tempEnvelope;
        tempEnvelope.setAttack(attackParam);
        tempEnvelope.setDecay(decayParam);
        tempEnvelope.setLength(lengthParam);

        int numPoints = static_cast<int>(envBounds.getWidth());
        auto envelopeCurve = tempEnvelope.getEnvelopeCurve(numPoints);
        int actualPoints = static_cast<int>(envelopeCurve.size());

        if (actualPoints > 0)
        {
            float centreY = envBounds.getCentreY();
            float halfHeight = envBounds.getHeight() * 0.45f;

            // Draw top half of envelope using quadratic curves for smoothness
            envelopeFill.startNewSubPath(envBounds.getX(), centreY);
            for (int i = 0; i < actualPoints; ++i)
            {
                float x = envBounds.getX() + i;
                float envValue = envelopeCurve[static_cast<size_t>(i)];
                float y = centreY - envValue * halfHeight;
                envelopeFill.lineTo(x, y);
            }

            // Draw bottom half in reverse
            for (int i = actualPoints - 1; i >= 0; --i)
            {
                float x = envBounds.getX() + i;
                float envValue = envelopeCurve[static_cast<size_t>(i)];
                float y = centreY + envValue * halfHeight;
                envelopeFill.lineTo(x, y);
            }
            envelopeFill.closeSubPath();

            // Smoother gradient for envelope fill
            juce::ColourGradient envGrad(
                juce::Colour(0x384a9eff), envBounds.getCentreX(), centreY - halfHeight,  // Slightly more opaque at top
                juce::Colour(0x104a9eff), envBounds.getCentreX(), centreY,               // Fade toward center
                false
            );
            envGrad.addColour(0.5, juce::Colour(0x204a9eff));  // Mid-point for smoother transition
            g.setGradientFill(envGrad);
            g.fillPath(envelopeFill);

            // Draw envelope outline (top curve) with glow for visibility
            juce::Path envelopeOutline;
            envelopeOutline.startNewSubPath(envBounds.getX(), centreY);
            for (int i = 0; i < actualPoints; ++i)
            {
                float x = envBounds.getX() + i;
                float envValue = envelopeCurve[static_cast<size_t>(i)];
                float y = centreY - envValue * halfHeight;
                envelopeOutline.lineTo(x, y);
            }

            // Glow behind top outline
            g.setColour(juce::Colour(0x404a9eff));
            g.strokePath(envelopeOutline, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                                                juce::PathStrokeType::rounded));

            // Brighter accent line for top envelope
            g.setColour(juce::Colour(0xff5ab0ff));  // Brighter blue
            g.strokePath(envelopeOutline, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                                juce::PathStrokeType::rounded));

            // Bottom outline with same treatment
            juce::Path bottomOutline;
            bottomOutline.startNewSubPath(envBounds.getX(), centreY);
            for (int i = 0; i < actualPoints; ++i)
            {
                float x = envBounds.getX() + i;
                float envValue = envelopeCurve[static_cast<size_t>(i)];
                float y = centreY + envValue * halfHeight;
                bottomOutline.lineTo(x, y);
            }

            // Glow behind bottom outline
            g.setColour(juce::Colour(0x404a9eff));
            g.strokePath(bottomOutline, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                                              juce::PathStrokeType::rounded));

            // Brighter accent line for bottom envelope
            g.setColour(juce::Colour(0xff5ab0ff));
            g.strokePath(bottomOutline, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                              juce::PathStrokeType::rounded));
        }
    }

    // Draw IR offset line (green)
    if (irOffsetParam > 0.001f)
    {
        float offsetX = waveformBounds.getX() + waveformBounds.getWidth() * irOffsetParam;
        g.setColour(irOffsetColour.withAlpha(0.8f));
        g.drawVerticalLine(static_cast<int>(offsetX), waveformBounds.getY(), waveformBounds.getBottom());

        // Shade the skipped area
        g.setColour(irOffsetColour.withAlpha(0.15f));
        g.fillRect(waveformBounds.getX(), waveformBounds.getY(),
                   offsetX - waveformBounds.getX(), waveformBounds.getHeight());

        // Label
        g.setColour(irOffsetColour);
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("OFFSET", static_cast<int>(offsetX - 35), static_cast<int>(waveformBounds.getY() + 2),
                   30, 12, juce::Justification::centredRight);
    }

    // Draw length cutoff line
    if (lengthParam < 1.0f)
    {
        float cutoffX = waveformBounds.getX() + waveformBounds.getWidth() * lengthParam;
        g.setColour(envelopeColour.withAlpha(0.7f));
        g.drawVerticalLine(static_cast<int>(cutoffX), waveformBounds.getY(), waveformBounds.getBottom());

        // Shade the truncated area
        g.setColour(backgroundColour.withAlpha(0.7f));
        g.fillRect(cutoffX, waveformBounds.getY(),
                   waveformBounds.getRight() - cutoffX, waveformBounds.getHeight());
    }

    // Draw filter envelope visualization (purple line showing filter sweep)
    if (filterEnvEnabled && hasWaveform)
    {
        // Draw filter envelope as a line from top to bottom of waveform
        // Y position represents cutoff frequency (high = top, low = bottom)
        auto filterBounds = waveformBounds.reduced(0, 10);
        int numPoints = static_cast<int>(filterBounds.getWidth());

        juce::Path filterPath;
        bool firstPoint = true;

        // Map frequency to Y position (log scale)
        auto freqToY = [&](float freq) -> float
        {
            float logMin = std::log(200.0f);
            float logMax = std::log(20000.0f);
            float logFreq = std::log(juce::jlimit(200.0f, 20000.0f, freq));
            float normalized = (logFreq - logMin) / (logMax - logMin);
            return filterBounds.getBottom() - normalized * filterBounds.getHeight();
        };

        for (int i = 0; i < numPoints; ++i)
        {
            float position = static_cast<float>(i) / static_cast<float>(numPoints);
            float x = filterBounds.getX() + i;

            // Calculate filter cutoff at this position
            float cutoff;
            if (position < filterEnvAttack)
            {
                cutoff = filterEnvInitFreq;
            }
            else
            {
                float sweepPos = (filterEnvAttack < 1.0f)
                    ? (position - filterEnvAttack) / (1.0f - filterEnvAttack)
                    : 1.0f;
                sweepPos = juce::jlimit(0.0f, 1.0f, sweepPos);
                float logInit = std::log(filterEnvInitFreq);
                float logEnd = std::log(filterEnvEndFreq);
                cutoff = std::exp(logInit + sweepPos * (logEnd - logInit));
            }

            float y = freqToY(cutoff);

            if (firstPoint)
            {
                filterPath.startNewSubPath(x, y);
                firstPoint = false;
            }
            else
            {
                filterPath.lineTo(x, y);
            }
        }

        g.setColour(filterEnvColour.withAlpha(0.8f));
        g.strokePath(filterPath, juce::PathStrokeType(2.0f));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        auto filterLabelBounds = juce::Rectangle<float>(waveformBounds.getX(), waveformBounds.getY(), 50, 15);
        g.drawText("FILTER", filterLabelBounds,
                   juce::Justification::topLeft);    }

    // Draw playback position indicator
    if (playbackPosition > 0.0f)
    {
        float posX = waveformBounds.getX() + waveformBounds.getWidth() * playbackPosition;
        g.setColour(positionColour);
        g.drawVerticalLine(static_cast<int>(posX), waveformBounds.getY(), waveformBounds.getBottom());
    }

    // Draw IR length label
    float lengthSec = (irSampleRate > 0)
        ? static_cast<float>(irBuffer.getNumSamples()) / static_cast<float>(irSampleRate)
        : 0.0f;
    juce::String lengthText = juce::String(lengthSec, 2) + "s";

    g.setColour(textColour);
    g.setFont(juce::Font(10.0f));
    g.drawText(lengthText, waveformBounds.removeFromBottom(15).removeFromRight(40),
               juce::Justification::centredRight);

    // Draw "Reversed" indicator if applicable
    if (reversed)
    {
        g.setColour(envelopeColour);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("REVERSED", bounds.reduced(10).removeFromTop(15),
                   juce::Justification::topRight);
    }
}

void IRWaveformDisplay::resized()
{
    if (hasWaveform)
    {
        rebuildWaveformPath();
        rebuildEnvelopePath();
    }
}

void IRWaveformDisplay::drawTimeGrid(juce::Graphics& g)
{
    if (!hasWaveform || irSampleRate <= 0.0 || irBuffer.getNumSamples() == 0)
        return;

    auto bounds = getLocalBounds().toFloat().reduced(10, 20);
    float totalSeconds = static_cast<float>(irBuffer.getNumSamples()) / static_cast<float>(irSampleRate);

    if (totalSeconds <= 0.0f)
        return;

    // Determine grid interval based on total length
    float gridInterval;
    if (totalSeconds <= 1.0f)
        gridInterval = 0.1f;
    else if (totalSeconds <= 3.0f)
        gridInterval = 0.5f;
    else if (totalSeconds <= 10.0f)
        gridInterval = 1.0f;
    else
        gridInterval = 2.0f;

    g.setColour(gridColour);
    g.setFont(juce::Font(9.0f));

    for (float t = 0.0f; t <= totalSeconds; t += gridInterval)
    {
        float x = bounds.getX() + (t / totalSeconds) * bounds.getWidth();

        // Grid line
        g.setColour(gridColour);
        g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());

        // Time label
        g.setColour(textColour);
        juce::String label;
        if (gridInterval < 1.0f)
            label = juce::String(static_cast<int>(t * 1000)) + "ms";
        else
            label = juce::String(t, 1) + "s";

        g.drawText(label, static_cast<int>(x - 20), static_cast<int>(bounds.getBottom() + 2),
                   40, 12, juce::Justification::centred);
    }
}

void IRWaveformDisplay::rebuildWaveformPath()
{
    waveformPath.clear();

    if (!hasWaveform || getWidth() <= 0 || getHeight() <= 0)
        return;

    auto bounds = getLocalBounds().toFloat().reduced(10, 20);
    int numSamples = irBuffer.getNumSamples();
    int numChannels = irBuffer.getNumChannels();

    if (numSamples == 0)
        return;

    // Downsample for display
    int pixelWidth = static_cast<int>(bounds.getWidth());
    int samplesPerPixel = std::max(1, numSamples / pixelWidth);

    float centreY = bounds.getCentreY();
    float amplitude = bounds.getHeight() * 0.45f;

    bool firstPoint = true;

    for (int pixel = 0; pixel < pixelWidth; ++pixel)
    {
        int startSample = pixel * samplesPerPixel;
        int endSample = std::min(startSample + samplesPerPixel, numSamples);

        float maxVal = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float* data = irBuffer.getReadPointer(channel);

            for (int i = startSample; i < endSample; ++i)
            {
                int sampleIndex = reversed ? (numSamples - 1 - i) : i;
                maxVal = std::max(maxVal, std::abs(data[sampleIndex]));
            }
        }

        float x = bounds.getX() + pixel;
        float y = centreY - maxVal * amplitude;

        if (firstPoint)
        {
            waveformPath.startNewSubPath(x, y);
            firstPoint = false;
        }
        else
        {
            waveformPath.lineTo(x, y);
        }
    }

    // Mirror for bottom half
    juce::Path bottomPath;
    firstPoint = true;

    for (int pixel = pixelWidth - 1; pixel >= 0; --pixel)
    {
        int startSample = pixel * samplesPerPixel;
        int endSample = std::min(startSample + samplesPerPixel, numSamples);

        float maxVal = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float* data = irBuffer.getReadPointer(channel);

            for (int i = startSample; i < endSample; ++i)
            {
                int sampleIndex = reversed ? (numSamples - 1 - i) : i;
                maxVal = std::max(maxVal, std::abs(data[sampleIndex]));
            }
        }

        float x = bounds.getX() + pixel;
        float y = centreY + maxVal * amplitude;

        waveformPath.lineTo(x, y);
    }

    waveformPath.closeSubPath();
}

void IRWaveformDisplay::rebuildEnvelopePath()
{
    envelopePath.clear();

    if (!hasWaveform || getWidth() <= 0 || getHeight() <= 0)
        return;

    auto bounds = getLocalBounds().toFloat().reduced(10, 20);
    int numPoints = static_cast<int>(bounds.getWidth());

    EnvelopeProcessor tempEnvelope;
    tempEnvelope.setAttack(attackParam);
    tempEnvelope.setDecay(decayParam);
    tempEnvelope.setLength(lengthParam);

    auto envelopeCurve = tempEnvelope.getEnvelopeCurve(numPoints);
    int actualPoints = static_cast<int>(envelopeCurve.size());

    if (actualPoints == 0)
        return;

    float topY = bounds.getY();
    float height = bounds.getHeight();

    bool firstPoint = true;

    for (int i = 0; i < actualPoints; ++i)
    {
        float x = bounds.getX() + i;
        float envValue = envelopeCurve[static_cast<size_t>(i)];

        // Draw envelope as top and bottom bounds
        float y = topY + (1.0f - envValue) * height * 0.5f;

        if (firstPoint)
        {
            envelopePath.startNewSubPath(x, y);
            firstPoint = false;
        }
        else
        {
            envelopePath.lineTo(x, y);
        }
    }
}

//==============================================================================
// Display Mode Methods
//==============================================================================

void IRWaveformDisplay::setDisplayMode(DisplayMode mode)
{
    if (displayMode != mode)
    {
        displayMode = mode;
        needsRepaint = true;
        if (onDisplayModeChanged)
            onDisplayModeChanged(mode);
    }
}

void IRWaveformDisplay::setEQParameters(float hpfFreq, float lpfFreq,
                                         float lowGain, float loMidGain, float hiMidGain, float highGain)
{
    eqHpfFreq = hpfFreq;
    eqLpfFreq = lpfFreq;
    eqLowGain = lowGain;
    eqLoMidGain = loMidGain;
    eqHiMidGain = hiMidGain;
    eqHighGain = highGain;

    if (displayMode == DisplayMode::EQCurve)
        needsRepaint = true;
}

void IRWaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    // Check if click is on the toggle buttons
    if (irToggleBounds.contains(e.getPosition()))
    {
        setDisplayMode(DisplayMode::IRWaveform);
    }
    else if (eqToggleBounds.contains(e.getPosition()))
    {
        setDisplayMode(DisplayMode::EQCurve);
    }
}

void IRWaveformDisplay::drawModeToggle(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Toggle positioned at top-right corner
    int toggleWidth = 70;
    int toggleHeight = 20;
    int toggleX = bounds.getRight() - toggleWidth - 10;
    int toggleY = 8;

    // Overall toggle background
    auto toggleBounds = juce::Rectangle<int>(toggleX, toggleY, toggleWidth, toggleHeight);
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(toggleBounds.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawRoundedRectangle(toggleBounds.toFloat(), 4.0f, 1.0f);

    // IR button (left half)
    irToggleBounds = juce::Rectangle<int>(toggleX, toggleY, toggleWidth / 2, toggleHeight);
    bool irActive = (displayMode == DisplayMode::IRWaveform);

    if (irActive)
    {
        g.setColour(accentColour);
        g.fillRoundedRectangle(irToggleBounds.toFloat().reduced(2), 3.0f);
    }
    g.setColour(irActive ? juce::Colours::white : textColour);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("IR", irToggleBounds, juce::Justification::centred);

    // EQ button (right half)
    eqToggleBounds = juce::Rectangle<int>(toggleX + toggleWidth / 2, toggleY, toggleWidth / 2, toggleHeight);
    bool eqActive = (displayMode == DisplayMode::EQCurve);

    if (eqActive)
    {
        g.setColour(accentColour);
        g.fillRoundedRectangle(eqToggleBounds.toFloat().reduced(2), 3.0f);
    }
    g.setColour(eqActive ? juce::Colours::white : textColour);
    g.drawText("EQ", eqToggleBounds, juce::Justification::centred);
}

float IRWaveformDisplay::calculateEQResponse(float freq)
{
    float response = 0.0f;

    // HPF response (12dB/oct approximation)
    if (eqHpfFreq > 20.0f && freq < eqHpfFreq * 4.0f)
    {
        float ratio = freq / eqHpfFreq;
        if (ratio < 1.0f)
            response -= 12.0f * std::log2(1.0f / ratio);
    }

    // LPF response (12dB/oct approximation)
    if (eqLpfFreq < 20000.0f && freq > eqLpfFreq * 0.25f)
    {
        float ratio = freq / eqLpfFreq;
        if (ratio > 1.0f)
            response -= 12.0f * std::log2(ratio);
    }

    // Bell/shelf EQ bands (approximation using Gaussian curves)
    auto bellResponse = [](float f, float centerFreq, float gain, float q = 1.0f) -> float
    {
        if (std::abs(gain) < 0.01f)
            return 0.0f;
        float octaveWidth = 1.5f / q;
        float octaveDistance = std::log2(f / centerFreq);
        float gaussian = std::exp(-0.5f * std::pow(octaveDistance / octaveWidth, 2.0f));
        return gain * gaussian;
    };

    // Low shelf (100 Hz)
    response += bellResponse(freq, lowFreq, eqLowGain, 0.7f);

    // Lo-Mid (600 Hz)
    response += bellResponse(freq, loMidFreq, eqLoMidGain, 1.0f);

    // Hi-Mid (3000 Hz)
    response += bellResponse(freq, hiMidFreq, eqHiMidGain, 1.0f);

    // High shelf (8000 Hz)
    response += bellResponse(freq, highFreq, eqHighGain, 0.7f);

    return juce::jlimit(-18.0f, 18.0f, response);
}

void IRWaveformDisplay::drawEQCurve(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const float minFreq = 20.0f;
    const float maxFreq = 20000.0f;
    const float dbRange = 15.0f;  // ±15 dB

    float centreY = bounds.getCentreY();

    // Helper functions
    auto freqToX = [&](float freq) -> float
    {
        float normalizedFreq = (std::log10(freq) - std::log10(minFreq)) /
                               (std::log10(maxFreq) - std::log10(minFreq));
        return bounds.getX() + normalizedFreq * bounds.getWidth();
    };

    auto dbToY = [&](float db) -> float
    {
        return centreY - (db / dbRange) * (bounds.getHeight() * 0.5f);
    };

    // Draw grid lines
    // Horizontal: 0dB, ±6dB, ±12dB
    g.setColour(juce::Colour(0xff2a2a2a));

    // 0dB line (brighter)
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawHorizontalLine(static_cast<int>(centreY), bounds.getX(), bounds.getRight());

    // ±6dB and ±12dB lines
    g.setColour(juce::Colour(0xff282828));
    for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
    {
        float y = dbToY(db);
        g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());
    }

    // Vertical frequency lines: 50, 100, 500, 1k, 5k, 10k
    const float freqMarkers[] = { 50.0f, 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f };
    const char* freqLabels[] = { "50", "100", "500", "1k", "5k", "10k" };

    for (int i = 0; i < 6; ++i)
    {
        float x = freqToX(freqMarkers[i]);
        g.setColour(juce::Colour(0xff282828));
        g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());

        // Frequency labels at bottom
        g.setColour(juce::Colour(0xff606060));
        g.setFont(juce::Font(9.0f));
        g.drawText(freqLabels[i], static_cast<int>(x) - 15, static_cast<int>(bounds.getBottom()) - 12,
                   30, 12, juce::Justification::centred);
    }

    // dB labels on left
    g.setColour(juce::Colour(0xff606060));
    g.setFont(juce::Font(9.0f));
    g.drawText("+12", static_cast<int>(bounds.getX()) + 2, static_cast<int>(dbToY(12.0f)) - 6, 25, 12, juce::Justification::left);
    g.drawText("+6", static_cast<int>(bounds.getX()) + 2, static_cast<int>(dbToY(6.0f)) - 6, 25, 12, juce::Justification::left);
    g.drawText("0", static_cast<int>(bounds.getX()) + 2, static_cast<int>(centreY) - 6, 25, 12, juce::Justification::left);
    g.drawText("-6", static_cast<int>(bounds.getX()) + 2, static_cast<int>(dbToY(-6.0f)) - 6, 25, 12, juce::Justification::left);
    g.drawText("-12", static_cast<int>(bounds.getX()) + 2, static_cast<int>(dbToY(-12.0f)) - 6, 25, 12, juce::Justification::left);

    // Build EQ response curve
    juce::Path curvePath;
    const int numPoints = 256;

    for (int i = 0; i < numPoints; ++i)
    {
        float normalizedX = static_cast<float>(i) / static_cast<float>(numPoints - 1);
        float freq = minFreq * std::pow(maxFreq / minFreq, normalizedX);
        float db = calculateEQResponse(freq);
        float x = bounds.getX() + normalizedX * bounds.getWidth();
        float y = dbToY(db);

        if (i == 0)
            curvePath.startNewSubPath(x, y);
        else
            curvePath.lineTo(x, y);
    }

    // Draw filled area under curve
    juce::Path fillPath = curvePath;
    fillPath.lineTo(bounds.getRight(), centreY);
    fillPath.lineTo(bounds.getX(), centreY);
    fillPath.closeSubPath();

    juce::ColourGradient fillGrad(
        accentColour.withAlpha(0.25f), bounds.getCentreX(), bounds.getY(),
        accentColour.withAlpha(0.05f), bounds.getCentreX(), bounds.getBottom(),
        false
    );
    g.setGradientFill(fillGrad);
    g.fillPath(fillPath);

    // Draw glow behind curve
    g.setColour(accentColour.withAlpha(0.3f));
    g.strokePath(curvePath, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    // Draw main curve
    g.setColour(accentColour);
    g.strokePath(curvePath, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    // Draw highlight on curve
    g.setColour(accentColour.brighter(0.4f).withAlpha(0.6f));
    g.strokePath(curvePath, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    // Draw EQ band markers (dots at fixed frequencies)
    auto drawBandDot = [&](float freq, float gain, const juce::String& label)
    {
        float x = freqToX(freq);
        float y = dbToY(gain);
        float dotRadius = 4.0f;

        // Glow
        g.setColour(accentColour.withAlpha(0.4f));
        g.fillEllipse(x - dotRadius - 2, y - dotRadius - 2, (dotRadius + 2) * 2, (dotRadius + 2) * 2);

        // Dot
        g.setColour(accentColour);
        g.fillEllipse(x - dotRadius, y - dotRadius, dotRadius * 2, dotRadius * 2);

        // Label above dot
        g.setColour(textColour.brighter(0.3f));
        g.setFont(juce::Font(8.0f));
        g.drawText(label, static_cast<int>(x) - 15, static_cast<int>(y) - 16, 30, 12, juce::Justification::centred);
    };

    drawBandDot(lowFreq, eqLowGain, "LOW");
    drawBandDot(loMidFreq, eqLoMidGain, "LO-M");
    drawBandDot(hiMidFreq, eqHiMidGain, "HI-M");
    drawBandDot(highFreq, eqHighGain, "HIGH");
}

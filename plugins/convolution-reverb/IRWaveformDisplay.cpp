/*
  ==============================================================================

    Convolution Reverb - IR Waveform Display
    Waveform visualization with envelope overlay
    Copyright (c) 2025 Luna Co. Audio

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

    if (!hasWaveform)
    {
        // No IR loaded message
        g.setColour(textColour);
        g.setFont(juce::Font(14.0f));
        g.drawText("No IR Loaded", bounds, juce::Justification::centred);
        g.setFont(juce::Font(11.0f));
        g.drawText("Select an impulse response from the browser",
                   bounds.translated(0, 20), juce::Justification::centred);
        return;
    }

    auto waveformBounds = bounds.reduced(10, 20);

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

    // Draw envelope overlay
    g.setColour(envelopeColour);
    g.strokePath(envelopePath, juce::PathStrokeType(2.0f));

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

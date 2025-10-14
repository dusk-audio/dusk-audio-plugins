/*
  ==============================================================================

    SpectrumAnalyzerNew.h
    Based on klangfreund/SpectrumAnalyser approach
    Using JUCE FFT only (no dRowAudio dependency)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

class SpectrumAnalyzerNew : public juce::Component,
                             private juce::Timer
{
public:
    SpectrumAnalyzerNew()
        : forwardFFT(fftOrder),
          window(fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        scopeData.fill(0.0f);
        magnitudes.fill(0.0f);

        setOpaque(true);
        startTimerHz(30); // 30 FPS refresh rate
    }

    ~SpectrumAnalyzerNew() override
    {
        stopTimer();
    }

    void pushBuffer(const juce::AudioBuffer<float>& buffer)
    {
        if (buffer.getNumChannels() == 0)
            return;

        const int numSamples = buffer.getNumSamples();
        const float* channelData = buffer.getReadPointer(0); // Mono from left channel

        for (int i = 0; i < numSamples; ++i)
            pushNextSampleIntoFifo(channelData[i]);
    }

    void setSampleRate(double newSampleRate)
    {
        sampleRate = newSampleRate;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);

        const int w = getWidth();
        const int h = getHeight();

        // Draw grid lines
        g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));

        // Vertical frequency lines
        const int frequencies[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
        for (int freq : frequencies)
        {
            float proportion = freq / (sampleRate * 0.5f);
            float x = logTransform(proportion) * w;
            g.drawVerticalLine(juce::roundToInt(x), 0.0f, (float)h);
        }

        // Horizontal dB lines
        for (int db = 0; db >= -60; db -= 12)
        {
            float y = juce::jmap((float)db, minDB, maxDB, (float)h, 0.0f);
            g.drawHorizontalLine(juce::roundToInt(y), 0.0f, (float)w);
        }

        // Draw the spectrum
        juce::Path spectrumPath;
        bool started = false;

        for (int i = 0; i < scopeSize; ++i)
        {
            float magnitude = scopeData[i];

            if (magnitude > 0.0001f) // Only draw visible parts
            {
                float proportion = (float)i / (float)scopeSize;
                float x = logTransform(proportion) * w;

                // Map magnitude to dB and then to screen height
                float db = magnitudeToDecibels(magnitude);
                float y = juce::jmap(db, minDB, maxDB, (float)h, 0.0f);

                if (!started)
                {
                    spectrumPath.startNewSubPath(x, y);
                    started = true;
                }
                else
                {
                    spectrumPath.lineTo(x, y);
                }
            }
        }

        g.setColour(juce::Colour(0, 255, 0)); // Green
        g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));
    }

private:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder; // 4096
    static constexpr int scopeSize = fftSize / 2;

    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    std::array<float, fftSize * 2> fftData{};
    std::array<float, fftSize> fifo{};
    std::array<float, scopeSize> magnitudes{};  // Peak-hold magnitudes
    std::array<float, scopeSize> scopeData{};   // Display data

    int fifoIndex = 0;
    std::atomic<bool> nextFFTBlockReady{false};

    double sampleRate = 44100.0;
    const float minDB = -90.0f;
    const float maxDB = 6.0f;

    // Logarithmic transform for frequency display (from klangfreund)
    static float logTransform(float proportion)
    {
        const float minimum = 1.0f;
        const float maximum = 1000.0f;
        return std::log10(minimum + ((maximum - minimum) * proportion)) / std::log10(maximum);
    }

    static float magnitudeToDecibels(float magnitude)
    {
        return magnitude > 0.0f ? 20.0f * std::log10(magnitude) : -100.0f;
    }

    void pushNextSampleIntoFifo(float sample) noexcept
    {
        if (fifoIndex == fftSize)
        {
            if (!nextFFTBlockReady.load())
            {
                std::copy(fifo.begin(), fifo.end(), fftData.begin());
                nextFFTBlockReady.store(true);
            }
            fifoIndex = 0;
        }

        fifo[static_cast<size_t>(fifoIndex++)] = sample;
    }

    void timerCallback() override
    {
        if (nextFFTBlockReady.exchange(false))
        {
            drawNextFrameOfSpectrum();
        }
        repaint();
    }

    void drawNextFrameOfSpectrum()
    {
        // Apply window
        window.multiplyWithWindowingTable(fftData.data(), fftSize);

        // Perform FFT
        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

        // Calculate magnitudes with proper scaling
        // Following klangfreund's approach: updateMagnitudesIfBigger
        const float scalingFactor = 2.0f / fftSize;

        for (int i = 0; i < scopeSize; ++i)
        {
            float rawMagnitude = fftData[i];
            float magnitude = rawMagnitude * scalingFactor;

            // Peak-hold: only update if new magnitude is bigger
            if (magnitude > magnitudes[i])
            {
                magnitudes[i] = magnitude;
            }
            else
            {
                // Slow decay for peak-hold effect
                magnitudes[i] *= 0.98f;
            }

            // Copy to display buffer with additional smoothing
            float targetValue = magnitudes[i];
            scopeData[i] = scopeData[i] * 0.9f + targetValue * 0.1f;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerNew)
};

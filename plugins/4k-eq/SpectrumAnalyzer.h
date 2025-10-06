#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

class SpectrumAnalyzer : public juce::Component,
                         private juce::Timer
{
public:
    SpectrumAnalyzer()
        : forwardFFT(fftOrder),
          window(fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        std::fill(scopeData.begin(), scopeData.end(), 0.0f);
        std::fill(scopeDataSmoothed.begin(), scopeDataSmoothed.end(), 0.0f);

        startTimerHz(30);  // 30 Hz refresh rate
    }

    void setSampleRate(double newSampleRate)
    {
        // Validate sample rate
        if (!std::isfinite(newSampleRate))
            return;

        // Clamp to reasonable audio range
        newSampleRate = juce::jlimit(8000.0, 192000.0, newSampleRate);
        sampleRate = newSampleRate;
    }

    ~SpectrumAnalyzer() override
    {
        stopTimer();
    }

    void pushBuffer(const juce::AudioBuffer<float>& buffer)
    {
        if (buffer.getNumChannels() == 0)
            return;

        const int numSamples = buffer.getNumSamples();

        // Validate sample count
        if (numSamples <= 0)
            return;

        // Mix to mono and push to FIFO
        // No per-sample clamping - normalization happens before FFT to preserve signal shape
        for (int i = 0; i < numSamples; ++i)
        {
            float monoSample = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                monoSample += buffer.getSample(ch, i);
            monoSample /= static_cast<float>(buffer.getNumChannels());

            pushSampleToFifo(monoSample);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a1a));

        auto bounds = getLocalBounds().toFloat();

        // Draw grid
        drawGrid(g, bounds);

        // Draw spectrum
        drawSpectrum(g, bounds);
    }

    void resized() override {}

    void timerCallback() override
    {
        if (nextFFTBlockReady.load())
        {
            performFFT();
            nextFFTBlockReady = false;
            repaint();
        }
    }

private:
    static constexpr int fftOrder = 11;  // 2048 samples
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int scopeSize = fftSize / 2;

    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    std::array<float, fftSize * 2> fftData;
    std::array<float, scopeSize> scopeData;
    std::array<float, scopeSize> scopeDataSmoothed;

    int fifoIndex = 0;
    std::atomic<bool> nextFFTBlockReady{false};

    double sampleRate = 48000.0;
    float minFreq = 20.0f;
    float maxFreq = 20000.0f;
    float minDB = -60.0f;  // More useful range for typical audio
    float maxDB = 6.0f;    // Allow for some headroom display

    void pushSampleToFifo(float sample)
    {
        if (fifoIndex == fftSize)
        {
            if (!nextFFTBlockReady.load())
            {
                // Copy data ready for FFT
                std::copy(fftData.begin(), fftData.begin() + fftSize, fftData.begin() + fftSize);
                nextFFTBlockReady = true;
            }
            fifoIndex = 0;
        }

        fftData[static_cast<size_t>(fifoIndex++)] = sample;
    }

    void performFFT()
    {
        // Normalize buffer before FFT to prevent clipping artifacts
        // Find peak absolute value in the FFT buffer
        float maxPeak = 0.0f;
        for (int i = 0; i < fftSize; ++i)
        {
            float absSample = std::abs(fftData[static_cast<size_t>(fftSize + i)]);
            if (absSample > maxPeak)
                maxPeak = absSample;
        }

        // Apply peak normalization if needed (only if signal exceeds ±1.0)
        // Use epsilon to guard against divide-by-zero
        constexpr float epsilon = 1e-9f;
        if (maxPeak > 1.0f)
        {
            float normFactor = 1.0f / (maxPeak + epsilon);
            for (int i = 0; i < fftSize; ++i)
                fftData[static_cast<size_t>(fftSize + i)] *= normFactor;
        }

        // Apply windowing
        window.multiplyWithWindowingTable(fftData.data() + fftSize, fftSize);

        // Perform FFT
        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data() + fftSize);

        // Convert to dB and smooth (SIMD-optimized)
        const float smoothing = 0.8f;
        const float oneMinusSmoothing = 1.0f - smoothing;

        // Process FFT bins to dB values
        for (int i = 0; i < scopeSize; ++i)
        {
            float level = fftData[static_cast<size_t>(fftSize + i)];
            level = juce::jlimit(0.0001f, 1.0f, level);
            // Convert to dB with reasonable offset for typical mix levels
            // Most audio hovers around -20 to -6 dB, so offset by -30 for better visibility
            float db = juce::Decibels::gainToDecibels(level) - 30.0f;
            scopeData[static_cast<size_t>(i)] = db;
        }

        // SIMD-optimized smoothing: smoothed = smoothed * a + data * (1-a)
        // Equivalent to: smoothed += (data - smoothed) * (1-a), but using multiply-add
        juce::FloatVectorOperations::multiply(scopeDataSmoothed.data(), smoothing, scopeSize);
        juce::FloatVectorOperations::addWithMultiply(scopeDataSmoothed.data(),
                                                     scopeData.data(),
                                                     oneMinusSmoothing,
                                                     scopeSize);
    }

    void drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        g.setColour(juce::Colour(0xff2a2a2a));

        // Frequency lines (log scale)
        std::array<float, 7> freqs = {100.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f};

        for (float freq : freqs)
        {
            float x = freqToX(freq, bounds);
            g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());
        }

        // dB lines
        for (float db = minDB; db <= maxDB; db += 10.0f)
        {
            float y = dbToY(db, bounds);
            g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());
        }

        // Labels
        g.setColour(juce::Colour(0xff808080));
        g.setFont(10.0f);

        for (float freq : freqs)
        {
            float x = freqToX(freq, bounds);
            juce::String label = freq >= 1000.0f ?
                juce::String(freq / 1000.0f, 1) + "k" :
                juce::String(static_cast<int>(freq));
            g.drawText(label, static_cast<int>(x - 15), static_cast<int>(bounds.getBottom() - 15),
                      30, 15, juce::Justification::centred);
        }
    }

    void drawSpectrum(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        juce::Path spectrumPath;

        bool started = false;

        for (int i = 1; i < scopeSize; ++i)
        {
            float freq = binToFreq(i);
            if (freq < minFreq || freq > maxFreq)
                continue;

            float x = freqToX(freq, bounds);
            float db = scopeDataSmoothed[static_cast<size_t>(i)];
            float y = dbToY(db, bounds);

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

        g.setColour(juce::Colour(0xff4080ff));
        g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));
    }

    float binToFreq(int bin) const
    {
        return bin * static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    }

    float freqToX(float freq, juce::Rectangle<float> bounds) const
    {
        float logMin = std::log10(minFreq);
        float logMax = std::log10(maxFreq);
        float logFreq = std::log10(freq);

        float normalized = (logFreq - logMin) / (logMax - logMin);
        return bounds.getX() + normalized * bounds.getWidth();
    }

    float dbToY(float db, juce::Rectangle<float> bounds) const
    {
        float normalized = juce::jmap(db, minDB, maxDB, 1.0f, 0.0f);
        return bounds.getY() + normalized * bounds.getHeight();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

class SpectrumAnalyzer : public juce::Component,
                         private juce::Timer
{
public:
    // EQ parameters structure for frequency response calculation
    struct EQParams
    {
        // HPF/LPF
        float hpfFreq = 20.0f;
        float lpfFreq = 20000.0f;

        // LF Band
        float lfGain = 0.0f;
        float lfFreq = 100.0f;
        bool lfBell = false;

        // LMF Band
        float lmGain = 0.0f;
        float lmFreq = 600.0f;
        float lmQ = 0.7f;

        // HMF Band
        float hmGain = 0.0f;
        float hmFreq = 2000.0f;
        float hmQ = 0.7f;

        // HF Band
        float hfGain = 0.0f;
        float hfFreq = 8000.0f;
        bool hfBell = false;

        bool bypass = false;
    };

    SpectrumAnalyzer()
        : forwardFFT(fftOrder),
          window(fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        // Initialize arrays
        std::fill(fifo.begin(), fifo.end(), 0.0f);
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        std::fill(scopeData.begin(), scopeData.end(), 0.0f);
        std::fill(eqCurveData.begin(), eqCurveData.end(), 0.0f);

        setOpaque(true);
        startTimerHz(30);  // 30 fps update rate
    }

    ~SpectrumAnalyzer() override
    {
        stopTimer();
    }

    void setSampleRate(double newSampleRate)
    {
        if (!std::isfinite(newSampleRate))
            return;

        sampleRate = juce::jlimit(8000.0, 192000.0, newSampleRate);
        eqCurveDirty = true;
    }

    void setEQParams(const EQParams& params)
    {
        eqParams = params;
        eqCurveDirty = true;
    }

    void pushBuffer(const juce::AudioBuffer<float>& buffer)
    {
        if (buffer.getNumChannels() == 0)
            return;

        const int numSamples = buffer.getNumSamples();
        if (numSamples <= 0)
            return;

        // Mix to mono and push samples to FIFO
        for (int i = 0; i < numSamples; ++i)
        {
            float monoSample = 0.0f;

            // Average all channels
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                monoSample += buffer.getSample(ch, i);

            monoSample /= static_cast<float>(buffer.getNumChannels());

            pushNextSampleIntoFifo(monoSample);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a1a));

        auto bounds = getLocalBounds().toFloat();

        // Update EQ curve if needed
        updateEQCurve();

        // Draw grid
        drawGrid(g, bounds);

        // Draw spectrum
        drawSpectrum(g, bounds);

        // Draw EQ response curve
        drawEQCurve(g, bounds);
    }

    void resized() override
    {
        // Nothing to do here
    }

private:
    // FFT configuration - use 2^12 = 4096 for better frequency resolution
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;  // 4096
    static constexpr int scopeSize = 512;          // Display resolution

    juce::dsp::FFT forwardFFT;                     // FFT processor
    juce::dsp::WindowingFunction<float> window;    // Windowing function for FFT

    std::array<float, fftSize> fifo;               // Circular buffer for incoming audio
    std::array<float, fftSize * 2> fftData;        // FFT working data
    int fifoIndex = 0;                             // Current position in FIFO
    std::atomic<bool> nextFFTBlockReady { false }; // Flag for FFT processing
    std::array<float, scopeSize> scopeData;        // Visual display data

    // EQ curve data
    std::array<float, 512> eqCurveData;
    EQParams eqParams;
    bool eqCurveDirty = true;

    double sampleRate = 48000.0;

    // Display ranges
    float minFreq = 20.0f;
    float maxFreq = 20000.0f;
    float mindB = -90.0f;
    float maxdB = 6.0f;

    void pushNextSampleIntoFifo(float sample) noexcept
    {
        // When we've filled the FIFO, copy it to fftData for processing
        if (fifoIndex == fftSize)
        {
            if (!nextFFTBlockReady.load())
            {
                std::copy(fifo.begin(), fifo.end(), fftData.begin());
                nextFFTBlockReady = true;
            }

            fifoIndex = 0;
        }

        fifo[static_cast<size_t>(fifoIndex++)] = sample;
    }

    void drawNextFrameOfSpectrum()
    {
        // Apply Hann window to reduce spectral leakage
        window.multiplyWithWindowingTable(fftData.data(), fftSize);

        // Perform FFT - this returns magnitudes in the first fftSize/2 bins
        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

        // Proper normalization for Hann window
        // Scale by 2/N for one-sided spectrum and compensate for Hann window (0.5 coherent gain)
        const float scalingFactor = 4.0f / static_cast<float>(fftSize);

        // Apply scaling to get correct magnitude values
        for (int i = 0; i < fftSize / 2; ++i)
        {
            fftData[static_cast<size_t>(i)] *= scalingFactor;
        }

        // Map FFT bins to logarithmic display bins
        for (int i = 0; i < scopeSize; ++i)
        {
            // Logarithmic frequency mapping: 20 Hz to 20 kHz
            float normalizedPos = static_cast<float>(i) / static_cast<float>(scopeSize - 1);
            float freq = minFreq * std::pow(maxFreq / minFreq, normalizedPos);

            // Find the corresponding FFT bin
            float binFloat = freq * fftSize / static_cast<float>(sampleRate);
            int bin = static_cast<int>(binFloat + 0.5f);  // Round to nearest bin
            bin = juce::jlimit(0, fftSize / 2 - 1, bin);

            // For higher frequencies, average multiple bins to reduce aliasing
            // Calculate how many Hz each display bin represents
            float nextNormalizedPos = static_cast<float>(i + 1) / static_cast<float>(scopeSize - 1);
            float nextFreq = minFreq * std::pow(maxFreq / minFreq, nextNormalizedPos);
            float freqBandwidth = nextFreq - freq;

            // How many FFT bins does this display bin cover?
            float binsPerDisplayBin = freqBandwidth / (static_cast<float>(sampleRate) / fftSize);

            // Get magnitude value with interpolation for smoother display
            float magnitude = 0.0f;

            // For smoother display, use quadratic interpolation between bins
            if (bin > 0 && bin < fftSize / 2 - 1)
            {
                // Get neighboring bins
                float prev = fftData[static_cast<size_t>(bin - 1)];
                float curr = fftData[static_cast<size_t>(bin)];
                float next = fftData[static_cast<size_t>(bin + 1)];

                // Find the peak using quadratic interpolation
                if (curr > prev && curr > next)
                {
                    // We have a local peak, interpolate for smoother display
                    float a = 0.5f * (prev - 2.0f * curr + next);
                    if (std::abs(a) > 1e-10f)
                    {
                        float b = 0.5f * (next - prev);
                        float peakOffset = -b / (2.0f * a);
                        peakOffset = juce::jlimit(-0.5f, 0.5f, peakOffset);

                        // Interpolated magnitude
                        magnitude = curr - 0.25f * b * peakOffset;
                    }
                    else
                    {
                        magnitude = curr;
                    }
                }
                else
                {
                    // Not a peak, use the actual value
                    magnitude = curr;
                }
            }
            else
            {
                magnitude = fftData[static_cast<size_t>(bin)];
            }

            // Convert magnitude to dB
            float db = (magnitude > 1e-10f) ? 20.0f * std::log10f(magnitude) : mindB;

            // VERY aggressive noise gating - only show strong signals
            // This should eliminate all the noise you're seeing
            if (db < -40.0f)  // Much higher threshold
            {
                db = mindB;
            }

            // Clamp to display range
            db = juce::jlimit(mindB, maxdB, db);

            // Map dB value to 0-1 range for display
            float displayValue = juce::jmap(db, mindB, maxdB, 0.0f, 1.0f);

            // Additional threshold on display value
            if (displayValue < 0.1f)  // Hide anything below 10% of range
            {
                displayValue = 0.0f;
            }

            // Apply very heavy smoothing with peak hold behavior
            float oldValue = scopeData[static_cast<size_t>(i)];

            // Peak hold with slow release
            float newValue;
            if (displayValue > oldValue)
            {
                // Rising edge - respond quickly
                const float attackSmoothing = 0.8f;  // Slightly slower attack
                newValue = oldValue * attackSmoothing + displayValue * (1.0f - attackSmoothing);
            }
            else
            {
                // Falling edge - decay very slowly for stable display
                const float releaseSmoothing = 0.99f;  // Even slower release
                newValue = oldValue * releaseSmoothing + displayValue * (1.0f - releaseSmoothing);
            }

            // Much more aggressive final threshold
            if (newValue < 0.1f)  // Hide anything below 10%
                newValue = 0.0f;

            scopeData[static_cast<size_t>(i)] = newValue;
        }
    }

    void timerCallback() override
    {
        if (nextFFTBlockReady.load())
        {
            drawNextFrameOfSpectrum();
            nextFFTBlockReady = false;
            repaint();
        }
    }

    void updateEQCurve()
    {
        if (!eqCurveDirty)
            return;

        const int numPoints = static_cast<int>(eqCurveData.size());

        for (int i = 0; i < numPoints; ++i)
        {
            float t = static_cast<float>(i) / (numPoints - 1);
            float freq = minFreq * std::pow(maxFreq / minFreq, t);

            float totalGainDB = 0.0f;

            if (!eqParams.bypass)
            {
                // HPF response
                if (freq < eqParams.hpfFreq)
                {
                    float ratio = freq / eqParams.hpfFreq;
                    totalGainDB += 20.0f * std::log10(ratio) * 3.0f;
                }

                // LPF response
                if (freq > eqParams.lpfFreq)
                {
                    float ratio = freq / eqParams.lpfFreq;
                    totalGainDB += -20.0f * std::log10(ratio) * 2.0f;
                }

                // LF Band
                if (std::abs(eqParams.lfGain) > 0.01f)
                {
                    totalGainDB += calculateBellOrShelfResponse(
                        freq, eqParams.lfFreq, 0.7f, eqParams.lfGain, eqParams.lfBell, false);
                }

                // LMF Band
                if (std::abs(eqParams.lmGain) > 0.01f)
                {
                    totalGainDB += calculateBellResponse(
                        freq, eqParams.lmFreq, eqParams.lmQ, eqParams.lmGain);
                }

                // HMF Band
                if (std::abs(eqParams.hmGain) > 0.01f)
                {
                    totalGainDB += calculateBellResponse(
                        freq, eqParams.hmFreq, eqParams.hmQ, eqParams.hmGain);
                }

                // HF Band
                if (std::abs(eqParams.hfGain) > 0.01f)
                {
                    totalGainDB += calculateBellOrShelfResponse(
                        freq, eqParams.hfFreq, 0.7f, eqParams.hfGain, eqParams.hfBell, true);
                }
            }

            eqCurveData[static_cast<size_t>(i)] = totalGainDB;
        }

        eqCurveDirty = false;
    }

    float calculateBellResponse(float freq, float centerFreq, float q, float gainDB) const
    {
        if (std::abs(gainDB) < 0.01f)
            return 0.0f;

        float w = freq / centerFreq;
        float w2 = w * w;

        float denom = 1.0f + (1.0f / (q * q)) * (w2 + 1.0f / w2 - 2.0f);
        float gain = std::pow(10.0f, gainDB / 20.0f);
        float mag = std::abs(1.0f + (gain - 1.0f) / denom);

        return 20.0f * std::log10(std::max(0.0001f, mag));
    }

    float calculateLowShelfResponse(float freq, float cornerFreq, float q, float gainDB) const
    {
        if (std::abs(gainDB) < 0.01f)
            return 0.0f;

        float w = freq / cornerFreq;
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w2 = w * w;

        float numerator = A * A + w2;
        float denominator = 1.0f + w2;
        float mag = std::sqrt(numerator / denominator);

        return 20.0f * std::log10(std::max(0.0001f, mag));
    }

    float calculateHighShelfResponse(float freq, float cornerFreq, float q, float gainDB) const
    {
        if (std::abs(gainDB) < 0.01f)
            return 0.0f;

        float w = freq / cornerFreq;
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w2 = w * w;

        float numerator = 1.0f + A * A * w2;
        float denominator = 1.0f + w2;
        float mag = std::sqrt(numerator / denominator);

        return 20.0f * std::log10(std::max(0.0001f, mag));
    }

    float calculateBellOrShelfResponse(float freq, float cornerFreq, float q, float gainDB, bool isBell, bool isHighShelf = false) const
    {
        if (isBell)
            return calculateBellResponse(freq, cornerFreq, q, gainDB);
        else
            return isHighShelf ? calculateHighShelfResponse(freq, cornerFreq, q, gainDB)
                               : calculateLowShelfResponse(freq, cornerFreq, q, gainDB);
    }

    void drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Draw frequency grid lines
        g.setColour(juce::Colour(0xff2a2a2a));

        std::array<float, 31> freqs = {
            20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f,
            100.0f, 125.0f, 160.0f, 200.0f, 250.0f, 315.0f,
            400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f,
            1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f,
            6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f
        };

        for (float freq : freqs)
        {
            if (freq > maxFreq)
                break;

            float x = freqToX(freq, bounds);

            bool isOctave = (freq == 31.5f || freq == 63.0f || freq == 125.0f ||
                            freq == 250.0f || freq == 500.0f || freq == 1000.0f ||
                            freq == 2000.0f || freq == 4000.0f || freq == 8000.0f ||
                            freq == 16000.0f);

            g.setColour(isOctave ? juce::Colour(0xff3a3a3a) : juce::Colour(0xff252525));
            g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());
        }

        // Draw dB grid lines
        for (float db = -90.0f; db <= 6.0f; db += 6.0f)
        {
            float y = juce::jmap(db, mindB, maxdB, bounds.getBottom(), bounds.getY());

            if (std::abs(db) < 0.1f)
                g.setColour(juce::Colour(0xff5a5a5a));
            else if (std::abs(db + 18.0f) < 0.1f || std::abs(db + 36.0f) < 0.1f)
                g.setColour(juce::Colour(0xff3a3a3a));
            else
                g.setColour(juce::Colour(0xff252525));

            g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());
        }

        // Draw frequency labels
        g.setColour(juce::Colour(0xffa0a0a0));
        g.setFont(10.0f);

        std::array<std::pair<float, juce::String>, 11> freqLabels = {{
            {50.0f, "50"},
            {100.0f, "100"},
            {200.0f, "200"},
            {500.0f, "500"},
            {1000.0f, "1k"},
            {2000.0f, "2k"},
            {5000.0f, "5k"},
            {10000.0f, "10k"},
            {15000.0f, "15k"},
            {20000.0f, "20k"}
        }};

        for (const auto& [freq, label] : freqLabels)
        {
            if (freq >= minFreq && freq <= maxFreq)
            {
                float x = freqToX(freq, bounds);
                g.drawText(label, static_cast<int>(x - 20), static_cast<int>(bounds.getBottom() - 18),
                          40, 18, juce::Justification::centred);
            }
        }

        // Draw dB labels
        g.setFont(9.0f);
        g.setColour(juce::Colour(0xffb0b0b0));

        std::array<float, 7> dbLabels = {{-90.0f, -60.0f, -36.0f, -18.0f, -6.0f, 0.0f, 6.0f}};
        for (float db : dbLabels)
        {
            if (db < mindB || db > maxdB)
                continue;

            float y = juce::jmap(db, mindB, maxdB, bounds.getBottom(), bounds.getY());
            juce::String label = juce::String(static_cast<int>(db));

            if (std::abs(db) < 0.1f)
                g.setColour(juce::Colour(0xffffff00));
            else if (std::abs(db + 18.0f) < 0.1f)
                g.setColour(juce::Colour(0xffc0c0c0));
            else
                g.setColour(juce::Colour(0xffa0a0a0));

            g.drawText(label, 2, static_cast<int>(y - 6), 25, 12, juce::Justification::right);
        }
    }

    void drawSpectrum(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        auto width = bounds.getWidth();
        auto bottom = bounds.getBottom();

        // Create path for spectrum line
        juce::Path spectrumLine;
        bool pathStarted = false;

        // Draw spectrum
        for (int i = 0; i < scopeSize; ++i)
        {
            float value = scopeData[static_cast<size_t>(i)];

            // Skip very low values
            if (value < 0.001f)
                continue;

            float x = bounds.getX() + (static_cast<float>(i) / static_cast<float>(scopeSize - 1)) * width;
            float y = juce::jmap(value, 0.0f, 1.0f, bottom, bounds.getY());

            if (!pathStarted)
            {
                spectrumLine.startNewSubPath(x, y);
                pathStarted = true;
            }
            else
            {
                spectrumLine.lineTo(x, y);
            }
        }

        if (pathStarted)
        {
            // Draw clean green line
            g.setColour(juce::Colour(0xff00ff88));
            g.strokePath(spectrumLine, juce::PathStrokeType(1.5f));
        }
    }

    void drawEQCurve(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        bool hasEQ = false;
        for (size_t i = 0; i < eqCurveData.size(); ++i)
        {
            if (std::abs(eqCurveData[i]) > 0.5f)
            {
                hasEQ = true;
                break;
            }
        }

        if (!hasEQ && !eqParams.bypass)
            return;

        juce::Path eqPath;
        const int numPoints = static_cast<int>(eqCurveData.size());

        for (int i = 0; i < numPoints; ++i)
        {
            float t = static_cast<float>(i) / (numPoints - 1);
            float freq = minFreq * std::pow(maxFreq / minFreq, t);

            float x = freqToX(freq, bounds);
            float eqGainDB = eqCurveData[static_cast<size_t>(i)];

            float centerDB = -20.0f;
            float y = juce::jmap(centerDB + eqGainDB, mindB, maxdB, bounds.getBottom(), bounds.getY());

            if (i == 0)
                eqPath.startNewSubPath(x, y);
            else
                eqPath.lineTo(x, y);
        }

        g.setColour(juce::Colour(0xffffaa00).withAlpha(0.9f));
        g.strokePath(eqPath, juce::PathStrokeType(2.0f));
    }

    float freqToX(float freq, juce::Rectangle<float> bounds) const
    {
        float logMin = std::log10(minFreq);
        float logMax = std::log10(maxFreq);
        float logFreq = std::log10(freq);

        float normalized = (logFreq - logMin) / (logMax - logMin);
        return bounds.getX() + normalized * bounds.getWidth();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};
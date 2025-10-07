#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

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
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        std::fill(scopeData.begin(), scopeData.end(), 0.0f);
        std::fill(scopeDataSmoothed.begin(), scopeDataSmoothed.end(), 0.0f);
        std::fill(eqCurveData.begin(), eqCurveData.end(), 0.0f);

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
        eqCurveDirty = true;
    }

    void setEQParams(const EQParams& params)
    {
        eqParams = params;
        eqCurveDirty = true;
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

        // Update EQ curve if parameters changed
        updateEQCurve();

        // Draw grid
        drawGrid(g, bounds);

        // Draw audio spectrum (behind EQ curve)
        drawSpectrum(g, bounds);

        // Draw EQ response curve (on top)
        drawEQCurve(g, bounds);
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
    std::array<float, 512> eqCurveData;  // Pre-calculated EQ curve for drawing

    int fifoIndex = 0;
    std::atomic<bool> nextFFTBlockReady{false};

    double sampleRate = 48000.0;
    float minFreq = 20.0f;
    float maxFreq = 20000.0f;
    float minDB = -60.0f;  // More useful range for typical audio
    float maxDB = 6.0f;    // Allow for some headroom display

    // Separate range for EQ curve display (centered around 0 dB)
    float eqMinDB = -20.0f;  // -20 dB at bottom
    float eqMaxDB = 20.0f;   // +20 dB at top

    EQParams eqParams;
    bool eqCurveDirty = true;  // Recalculate EQ curve when parameters change

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
        // Higher smoothing = more stable display, less responsive
        const float smoothing = 0.92f;  // Increased from 0.8 for more stability
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

    void updateEQCurve()
    {
        if (!eqCurveDirty)
            return;

        // Calculate frequency response for display
        const int numPoints = static_cast<int>(eqCurveData.size());

        for (int i = 0; i < numPoints; ++i)
        {
            // Logarithmically spaced frequency
            float t = static_cast<float>(i) / (numPoints - 1);
            float freq = minFreq * std::pow(maxFreq / minFreq, t);

            // Calculate combined frequency response in dB
            float totalGainDB = 0.0f;

            if (!eqParams.bypass)
            {
                // HPF response (18 dB/oct = 3rd order)
                if (freq < eqParams.hpfFreq)
                {
                    float ratio = freq / eqParams.hpfFreq;
                    totalGainDB += 20.0f * std::log10(ratio) * 3.0f;  // 3rd order = 18dB/oct
                }

                // LPF response (12 dB/oct = 2nd order)
                if (freq > eqParams.lpfFreq)
                {
                    float ratio = freq / eqParams.lpfFreq;
                    totalGainDB += -20.0f * std::log10(ratio) * 2.0f;  // 2nd order = 12dB/oct
                }

                // LF Band (low shelf or bell)
                if (std::abs(eqParams.lfGain) > 0.01f)
                {
                    totalGainDB += calculateBellOrShelfResponse(
                        freq, eqParams.lfFreq, 0.7f, eqParams.lfGain, eqParams.lfBell, false);
                }

                // LMF Band (always bell)
                if (std::abs(eqParams.lmGain) > 0.01f)
                {
                    totalGainDB += calculateBellResponse(
                        freq, eqParams.lmFreq, eqParams.lmQ, eqParams.lmGain);
                }

                // HMF Band (always bell)
                if (std::abs(eqParams.hmGain) > 0.01f)
                {
                    totalGainDB += calculateBellResponse(
                        freq, eqParams.hmFreq, eqParams.hmQ, eqParams.hmGain);
                }

                // HF Band (high shelf or bell)
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

    // Approximate bell filter response
    float calculateBellResponse(float freq, float centerFreq, float q, float gainDB) const
    {
        if (std::abs(gainDB) < 0.01f)
            return 0.0f;

        float w = freq / centerFreq;
        float w2 = w * w;

        // Bell/peak filter magnitude approximation
        float denom = 1.0f + (1.0f / (q * q)) * (w2 + 1.0f / w2 - 2.0f);
        float gain = std::pow(10.0f, gainDB / 20.0f);
        float mag = std::abs(1.0f + (gain - 1.0f) / denom);

        return 20.0f * std::log10(std::max(0.0001f, mag));
    }

    // Approximate low shelf response
    float calculateLowShelfResponse(float freq, float cornerFreq, float q, float gainDB) const
    {
        if (std::abs(gainDB) < 0.01f)
            return 0.0f;

        float w = freq / cornerFreq;
        float A = std::pow(10.0f, gainDB / 40.0f);  // amplitude factor
        float w2 = w * w;

        // Low shelf: gain applied at low frequencies, flat at high frequencies
        // H(w) = A * sqrt((1 + w^2) / (A^2 + w^2))
        float numerator = A * A + w2;
        float denominator = 1.0f + w2;
        float mag = A * std::sqrt(numerator / denominator);

        return 20.0f * std::log10(std::max(0.0001f, mag));
    }

    // Approximate high shelf response
    float calculateHighShelfResponse(float freq, float cornerFreq, float q, float gainDB) const
    {
        if (std::abs(gainDB) < 0.01f)
            return 0.0f;

        float w = freq / cornerFreq;
        float A = std::pow(10.0f, gainDB / 40.0f);  // amplitude factor
        float w2 = w * w;

        // High shelf: flat at low frequencies, gain applied at high frequencies
        // H(w) = A * sqrt((A^2 + w^2) / (1 + w^2))
        float numerator = A * A + w2;
        float denominator = 1.0f + w2;
        float mag = std::sqrt(numerator / denominator);

        return 20.0f * std::log10(std::max(0.0001f, mag));
    }

    // Approximate shelf or bell response
    float calculateBellOrShelfResponse(float freq, float cornerFreq, float q, float gainDB, bool isBell, bool isHighShelf = false) const
    {
        if (isBell)
        {
            return calculateBellResponse(freq, cornerFreq, q, gainDB);
        }
        else
        {
            if (isHighShelf)
                return calculateHighShelfResponse(freq, cornerFreq, q, gainDB);
            else
                return calculateLowShelfResponse(freq, cornerFreq, q, gainDB);
        }
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

        // Draw spectrum with transparency so EQ curve shows through
        g.setColour(juce::Colour(0xff4080ff).withAlpha(0.6f));
        g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));
    }

    void drawEQCurve(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        juce::Path eqPath;

        const int numPoints = static_cast<int>(eqCurveData.size());

        for (int i = 0; i < numPoints; ++i)
        {
            // Calculate frequency for this point
            float t = static_cast<float>(i) / (numPoints - 1);
            float freq = minFreq * std::pow(maxFreq / minFreq, t);

            float x = freqToX(freq, bounds);
            float db = eqCurveData[static_cast<size_t>(i)];
            float y = eqDbToY(db, bounds);  // Use EQ-specific mapping (centered around 0 dB)

            if (i == 0)
                eqPath.startNewSubPath(x, y);
            else
                eqPath.lineTo(x, y);
        }

        // Draw 0 dB reference line (should be in the middle)
        float zeroDbY = eqDbToY(0.0f, bounds);
        g.setColour(juce::Colour(0xff404040));  // Dark gray
        g.drawHorizontalLine(static_cast<int>(zeroDbY), bounds.getX(), bounds.getRight());

        // Draw EQ curve in bright yellow/green for visibility
        g.setColour(juce::Colour(0xffffff00));  // Bright yellow
        g.strokePath(eqPath, juce::PathStrokeType(2.0f));  // Thicker line

        // Optional: Draw semi-transparent fill to show EQ gain/cut regions
        juce::Path fillPath = eqPath;
        fillPath.lineTo(bounds.getRight(), zeroDbY);
        fillPath.lineTo(bounds.getX(), zeroDbY);
        fillPath.closeSubPath();

        g.setColour(juce::Colour(0xffffff00).withAlpha(0.1f));  // Very transparent yellow
        g.fillPath(fillPath);
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
        // Map dB to Y coordinate: higher dB (loud) should be at top (low Y value)
        // minDB (-60) should be at bottom (normalized = 1.0)
        // maxDB (+6) should be at top (normalized = 0.0)
        float normalized = juce::jmap(db, minDB, maxDB, 1.0f, 0.0f);
        return bounds.getY() + normalized * bounds.getHeight();
    }

    float eqDbToY(float db, juce::Rectangle<float> bounds) const
    {
        // Map EQ gain to Y coordinate (centered around 0 dB)
        // eqMaxDB (+20) should be at top (normalized = 0.0)
        // 0 dB should be in middle (normalized = 0.5)
        // eqMinDB (-20) should be at bottom (normalized = 1.0)
        float normalized = juce::jmap(db, eqMinDB, eqMaxDB, 1.0f, 0.0f);
        return bounds.getY() + normalized * bounds.getHeight();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};

#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <algorithm>

//==============================================================================
/**
    EQMatchProcessor — Captures reference spectrum and fits parametric EQ bands.

    Workflow:
    1. captureReference() — snapshot the current analyzer magnitudes as reference
    2. captureTarget() — snapshot the current analyzer magnitudes as target
    3. computeMatch() — fit N parametric bands to the difference curve
    4. getMatchedBands() — retrieve the fitted band parameters
*/
class EQMatchProcessor
{
public:
    static constexpr int NUM_BINS = 2048;
    static constexpr int MAX_FIT_BANDS = 6;  // Bands 2-7 in Multi-Q

    struct FittedBand
    {
        float freq = 1000.0f;
        float gainDB = 0.0f;
        float q = 1.0f;
        bool active = false;
    };

    EQMatchProcessor() = default;

    void setSampleRate(double sr) { sampleRate = sr; }

    /** Capture the reference spectrum (what you want to sound like). */
    void captureReference(const std::array<float, NUM_BINS>& magnitudes)
    {
        referenceMagnitudes = magnitudes;
        hasReference = true;
    }

    /** Capture the target spectrum (what your signal currently sounds like). */
    void captureTarget(const std::array<float, NUM_BINS>& magnitudes)
    {
        targetMagnitudes = magnitudes;
        hasTarget = true;
    }

    bool isReferenceSet() const { return hasReference; }
    bool isTargetSet() const { return hasTarget; }

    void clearReference() { hasReference = false; }
    void clearTarget() { hasTarget = false; }

    /** Get the difference curve (reference - target) in dB. */
    const std::array<float, NUM_BINS>& getDifferenceCurve() const { return differenceCurve; }

    /** Compute matched EQ parameters. Returns the number of active bands fitted. */
    int computeMatch(int maxBands = MAX_FIT_BANDS, float matchStrength = 1.0f)
    {
        if (!hasReference || !hasTarget)
            return 0;

        maxBands = std::min(maxBands, MAX_FIT_BANDS);

        // Compute difference curve (reference - target = what we need to add)
        for (int i = 0; i < NUM_BINS; ++i)
        {
            float ref = referenceMagnitudes[static_cast<size_t>(i)];
            float tgt = targetMagnitudes[static_cast<size_t>(i)];
            // Both are in dB (typically -100 to 0)
            differenceCurve[static_cast<size_t>(i)] = (ref - tgt) * matchStrength;
        }

        // Working copy of residual (what hasn't been fitted yet)
        std::array<float, NUM_BINS> residual = differenceCurve;

        // Clear previous fit
        for (auto& band : fittedBands)
            band = FittedBand{};

        int bandsUsed = 0;

        // Greedy fitting: find peak residual, place a band, subtract, repeat
        for (int b = 0; b < maxBands; ++b)
        {
            // Find the bin with the largest absolute residual (in useful range)
            int peakBin = -1;
            float peakValue = 0.0f;

            // Only search in useful frequency range (bins corresponding to ~30Hz-18kHz)
            int minBin = frequencyToBin(30.0f);
            int maxBin = frequencyToBin(18000.0f);
            minBin = std::max(1, minBin);
            maxBin = std::min(NUM_BINS - 1, maxBin);

            for (int i = minBin; i <= maxBin; ++i)
            {
                float absVal = std::abs(residual[static_cast<size_t>(i)]);
                if (absVal > peakValue)
                {
                    peakValue = absVal;
                    peakBin = i;
                }
            }

            // Stop if remaining error is small
            if (peakBin < 0 || peakValue < 1.0f)  // Less than 1 dB residual
                break;

            float peakFreq = binToFrequency(peakBin);
            float peakGain = residual[static_cast<size_t>(peakBin)];

            // Estimate Q from the width of the peak/dip in the residual
            float q = estimateQ(residual, peakBin);

            // Clamp gain to reasonable range
            peakGain = juce::jlimit(-24.0f, 24.0f, peakGain);
            peakFreq = juce::jlimit(20.0f, 20000.0f, peakFreq);
            q = juce::jlimit(0.1f, 18.0f, q);

            fittedBands[static_cast<size_t>(b)] = { peakFreq, peakGain, q, true };
            bandsUsed++;

            // Subtract this band's contribution from the residual
            subtractBandFromResidual(residual, peakFreq, peakGain, q);
        }

        return bandsUsed;
    }

    /** Get the fitted bands. */
    const std::array<FittedBand, MAX_FIT_BANDS>& getMatchedBands() const { return fittedBands; }

private:
    double sampleRate = 44100.0;
    bool hasReference = false;
    bool hasTarget = false;

    std::array<float, NUM_BINS> referenceMagnitudes{};
    std::array<float, NUM_BINS> targetMagnitudes{};
    std::array<float, NUM_BINS> differenceCurve{};
    std::array<FittedBand, MAX_FIT_BANDS> fittedBands{};

    /** Convert a frequency to the nearest FFT bin index. */
    int frequencyToBin(float freq) const
    {
        // Analyzer bins span 0 to Nyquist
        float nyquist = static_cast<float>(sampleRate * 0.5);
        int bin = static_cast<int>(freq / nyquist * static_cast<float>(NUM_BINS));
        return juce::jlimit(0, NUM_BINS - 1, bin);
    }

    /** Convert an FFT bin index to a frequency. */
    float binToFrequency(int bin) const
    {
        float nyquist = static_cast<float>(sampleRate * 0.5);
        return static_cast<float>(bin) / static_cast<float>(NUM_BINS) * nyquist;
    }

    /** Estimate Q by measuring the width of the residual peak at -3dB from peak. */
    float estimateQ(const std::array<float, NUM_BINS>& residual, int peakBin) const
    {
        float peakVal = std::abs(residual[static_cast<size_t>(peakBin)]);
        float halfPower = peakVal * 0.707f;  // -3dB point
        bool isBoost = residual[static_cast<size_t>(peakBin)] > 0;

        // Search left for -3dB point
        int leftBin = peakBin;
        for (int i = peakBin - 1; i >= 0; --i)
        {
            float val = isBoost ? residual[static_cast<size_t>(i)] : -residual[static_cast<size_t>(i)];
            if (val < halfPower)
            {
                leftBin = i;
                break;
            }
        }

        // Search right for -3dB point
        int rightBin = peakBin;
        for (int i = peakBin + 1; i < NUM_BINS; ++i)
        {
            float val = isBoost ? residual[static_cast<size_t>(i)] : -residual[static_cast<size_t>(i)];
            if (val < halfPower)
            {
                rightBin = i;
                break;
            }
        }

        // Q = center frequency / bandwidth
        float centerFreq = binToFrequency(peakBin);
        float lowFreq = binToFrequency(leftBin);
        float highFreq = binToFrequency(rightBin);
        float bandwidth = highFreq - lowFreq;

        if (bandwidth < 1.0f)
            return 10.0f;  // Very narrow — high Q

        return centerFreq / bandwidth;
    }

    /** Subtract a peaking filter's contribution from the residual. */
    void subtractBandFromResidual(std::array<float, NUM_BINS>& residual,
                                   float freq, float gainDB, float q) const
    {
        // Compute the peaking filter's magnitude response at each bin
        // Using the actual biquad transfer function for accuracy
        double A = std::pow(10.0, gainDB / 40.0);
        double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
        double alpha = std::sin(w0) / (2.0 * q);

        double b0 = 1.0 + alpha * A;
        double b1 = -2.0 * std::cos(w0);
        double b2 = 1.0 - alpha * A;
        double a0 = 1.0 + alpha / A;
        double a1 = -2.0 * std::cos(w0);
        double a2 = 1.0 - alpha / A;

        // Normalize
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 /= a0; a2 /= a0;

        for (int i = 0; i < NUM_BINS; ++i)
        {
            float binFreq = binToFrequency(i);
            double w = 2.0 * juce::MathConstants<double>::pi * binFreq / sampleRate;
            double cosw = std::cos(w);
            double cos2w = 2.0 * cosw * cosw - 1.0;
            double sinw = std::sin(w);
            double sin2w = 2.0 * sinw * cosw;

            double numR = b0 + b1 * cosw + b2 * cos2w;
            double numI = -(b1 * sinw + b2 * sin2w);
            double denR = 1.0 + a1 * cosw + a2 * cos2w;
            double denI = -(a1 * sinw + a2 * sin2w);

            double numMagSq = numR * numR + numI * numI;
            double denMagSq = denR * denR + denI * denI;

            double mag = (denMagSq > 1e-20) ? std::sqrt(numMagSq / denMagSq) : 1.0;
            float magDB = static_cast<float>(20.0 * std::log10(std::max(mag, 1e-10)));

            residual[static_cast<size_t>(i)] -= magDB;
        }
    }
};

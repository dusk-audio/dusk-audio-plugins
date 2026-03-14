// Standalone test for EQMatchProcessor — validates the full Match EQ pipeline.
// Build: cmake --build . --target MultiQMatchTest -j8
// Run:   ./plugins/multi-q/MultiQMatchTest

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <iostream>
#include <vector>
#include <cmath>
#include <array>
#include <random>

#include "EQMatchProcessor.h"

//==============================================================================
// Test framework
static int totalTests = 0;
static int passedTests = 0;
static int failedTests = 0;

static bool check(const char* name, bool condition)
{
    totalTests++;
    if (condition)
    {
        std::cout << "  PASS: " << name << std::endl;
        passedTests++;
    }
    else
    {
        std::cout << "  FAIL: " << name << std::endl;
        failedTests++;
    }
    return condition;
}

//==============================================================================
// Generate deterministic white noise using a Mersenne Twister
static void generateWhiteNoise(float* buffer, int length, unsigned int seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int i = 0; i < length; ++i)
        buffer[i] = dist(rng);
}

//==============================================================================
// Apply a low-shelf biquad filter in-place
// Robert Bristow-Johnson Audio EQ Cookbook formulas
static void applyLowShelf(float* buffer, int length, double sampleRate,
                          double freqHz, double gainDB)
{
    double A = std::pow(10.0, gainDB / 40.0); // sqrt of linear gain
    double w0 = 2.0 * M_PI * freqHz / sampleRate;
    double cosw0 = std::cos(w0);
    double sinw0 = std::sin(w0);
    double alpha = sinw0 / 2.0 * std::sqrt(2.0); // S = 1 (slope parameter)

    double sqrtA = std::sqrt(A);
    double b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha);
    double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
    double b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha);
    double a0 = (A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha;
    double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
    double a2 = (A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha;

    // Normalize
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0;

    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int i = 0; i < length; ++i)
    {
        double x0 = static_cast<double>(buffer[i]);
        double y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        buffer[i] = static_cast<float>(y0);
        x2 = x1; x1 = x0;
        y2 = y1; y1 = y0;
    }
}

//==============================================================================
// Measure spectrum using Welch's method (averaged periodogram with Hann window)
// Returns power in dB for each bin
static void measureSpectrum(const float* signal, int signalLength,
                            std::array<float, EQMatchProcessor::NUM_BINS>& outDB)
{
    static constexpr int ORDER = EQMatchProcessor::FFT_ORDER;
    static constexpr int SIZE = EQMatchProcessor::FFT_SIZE;
    static constexpr int BINS = EQMatchProcessor::NUM_BINS;
    static constexpr int HOP = SIZE / 2;

    juce::dsp::FFT fft(ORDER);
    juce::dsp::WindowingFunction<float> window(
        static_cast<size_t>(SIZE), juce::dsp::WindowingFunction<float>::hann);

    std::array<double, BINS> powerSum{};
    int frameCount = 0;

    std::vector<float> fftBuf(static_cast<size_t>(SIZE * 2), 0.0f);

    for (int pos = 0; pos + SIZE <= signalLength; pos += HOP)
    {
        // Copy frame
        std::copy(signal + pos, signal + pos + SIZE, fftBuf.begin());
        std::fill(fftBuf.begin() + SIZE, fftBuf.end(), 0.0f);

        // Window
        window.multiplyWithWindowingTable(fftBuf.data(), static_cast<size_t>(SIZE));

        // FFT
        fft.performRealOnlyForwardTransform(fftBuf.data());

        // Accumulate power
        for (int k = 0; k < BINS; ++k)
        {
            float re = fftBuf[static_cast<size_t>(k * 2)];
            float im = fftBuf[static_cast<size_t>(k * 2 + 1)];
            powerSum[static_cast<size_t>(k)] += static_cast<double>(re * re + im * im);
        }
        frameCount++;
    }

    // Convert to dB
    double invCount = (frameCount > 0) ? 1.0 / static_cast<double>(frameCount) : 1.0;
    for (int k = 0; k < BINS; ++k)
    {
        double avgPower = powerSum[static_cast<size_t>(k)] * invCount;
        double mag = std::sqrt(avgPower + 1e-30);
        outDB[static_cast<size_t>(k)] = static_cast<float>(20.0 * std::log10(mag + 1e-30));
    }
}

//==============================================================================
// Simple time-domain convolution (brute force, not performance-critical)
static void convolve(const float* signal, int sigLen,
                     const float* ir, int irLen,
                     float* output, int outLen)
{
    std::fill(output, output + outLen, 0.0f);
    for (int n = 0; n < outLen; ++n)
    {
        double sum = 0.0;
        for (int k = 0; k < irLen; ++k)
        {
            int sigIdx = n - k;
            if (sigIdx >= 0 && sigIdx < sigLen)
                sum += static_cast<double>(signal[sigIdx]) * static_cast<double>(ir[k]);
        }
        output[n] = static_cast<float>(sum);
    }
}

//==============================================================================
// Compute average dB in a frequency range (Hz)
static float averageDBInRange(const std::array<float, EQMatchProcessor::NUM_BINS>& spectrum,
                              double sampleRate, float lowHz, float highHz)
{
    float nyquist = static_cast<float>(sampleRate * 0.5);
    float binWidth = nyquist / static_cast<float>(EQMatchProcessor::NUM_BINS - 1);

    int lowBin = std::max(1, static_cast<int>(lowHz / binWidth));
    int highBin = std::min(EQMatchProcessor::NUM_BINS - 1, static_cast<int>(highHz / binWidth));

    if (highBin <= lowBin) return 0.0f;

    float sum = 0.0f;
    for (int k = lowBin; k <= highBin; ++k)
        sum += spectrum[static_cast<size_t>(k)];

    return sum / static_cast<float>(highBin - lowBin + 1);
}

//==============================================================================
int main()
{
    // Initialize JUCE (needed for FFT, WindowingFunction, etc.)
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "=== EQMatchProcessor Pipeline Test ===" << std::endl;
    std::cout << "Validates the full learn -> compute -> validate pipeline" << std::endl << std::endl;

    // Parameters
    const double sampleRate = 44100.0;
    const int blockSize = 512;
    const int signalLength = static_cast<int>(sampleRate * 2.0); // 2 seconds
    const double shelfFreq = 1000.0;
    const double shelfGainDB = -6.0;

    std::cout << "Signal: " << signalLength << " samples (" << signalLength / sampleRate << "s)"
              << " at " << sampleRate << " Hz" << std::endl;
    std::cout << "Applied filter: Low-shelf " << shelfGainDB << " dB at " << shelfFreq << " Hz"
              << std::endl << std::endl;

    // --- Generate signals ---
    std::cout << "--- Signal Generation ---" << std::endl;

    std::vector<float> referenceSignal(static_cast<size_t>(signalLength));
    std::vector<float> currentSignal(static_cast<size_t>(signalLength));

    // Same noise source for both
    generateWhiteNoise(referenceSignal.data(), signalLength, 42);
    std::copy(referenceSignal.begin(), referenceSignal.end(), currentSignal.begin());

    // Apply low-shelf to current signal
    applyLowShelf(currentSignal.data(), signalLength, sampleRate, shelfFreq, shelfGainDB);
    std::cout << "  Generated reference (flat white noise) and current (low-shelf filtered)" << std::endl;

    // Verify the filter did something
    std::array<float, EQMatchProcessor::NUM_BINS> refSpectrum{}, curSpectrum{};
    measureSpectrum(referenceSignal.data(), signalLength, refSpectrum);
    measureSpectrum(currentSignal.data(), signalLength, curSpectrum);

    float refLow = averageDBInRange(refSpectrum, sampleRate, 100.0f, 500.0f);
    float curLow = averageDBInRange(curSpectrum, sampleRate, 100.0f, 500.0f);
    float refHigh = averageDBInRange(refSpectrum, sampleRate, 4000.0f, 8000.0f);
    float curHigh = averageDBInRange(curSpectrum, sampleRate, 4000.0f, 8000.0f);

    std::cout << "  Reference: low=" << refLow << " dB, high=" << refHigh << " dB" << std::endl;
    std::cout << "  Current:   low=" << curLow << " dB, high=" << curHigh << " dB" << std::endl;
    std::cout << "  Difference (ref-cur) at low freq: " << (refLow - curLow) << " dB" << std::endl;

    check("Low-shelf filter created >3 dB difference below 500 Hz",
          (refLow - curLow) > 3.0f);

    // --- Learning Phase ---
    std::cout << std::endl << "--- Learning Phase ---" << std::endl;

    EQMatchProcessor processor;
    processor.prepare(sampleRate, blockSize);
    std::cout << "  Processor prepared" << std::endl;

    // Learn reference
    processor.startLearningReference();
    check("isLearningReference() after startLearningReference()", processor.isLearningReference());
    check("isLearning() during reference learning", processor.isLearning());

    for (int pos = 0; pos < signalLength; pos += blockSize)
    {
        int n = std::min(blockSize, signalLength - pos);
        processor.feedLearningBlock(referenceSignal.data() + pos, n);
    }
    processor.stopLearning();

    int refFrames = processor.getLearningFrameCount();
    std::cout << "  Reference: " << refFrames << " frames learned" << std::endl;
    check("Reference spectrum valid", processor.hasReferenceSpectrum());
    check("Reference frame count >= 3", refFrames >= 3);
    check("Not learning after stop", !processor.isLearning());

    // Learn current
    processor.startLearningCurrent();
    check("isLearningCurrent() after startLearningCurrent()", processor.isLearningCurrent());

    for (int pos = 0; pos < signalLength; pos += blockSize)
    {
        int n = std::min(blockSize, signalLength - pos);
        processor.feedLearningBlock(currentSignal.data() + pos, n);
    }
    processor.stopLearning();

    int curFrames = processor.getLearningFrameCount();
    std::cout << "  Current: " << curFrames << " frames learned" << std::endl;
    check("Current spectrum valid", processor.hasCurrentSpectrum());
    check("Current frame count >= 3", curFrames >= 3);

    // --- Correction Computation ---
    std::cout << std::endl << "--- Correction Computation ---" << std::endl;

    bool computed = processor.computeCorrection(
        3.0f,   // smoothingSemitones
        1.0f,   // applyAmount (100%)
        20.0f,  // limitBoostDB
        -20.0f, // limitCutDB
        true    // minimumPhase
    );

    check("computeCorrection() returned true", computed);

    if (!computed)
    {
        std::cout << std::endl << "*** FATAL: computeCorrection() failed, cannot continue ***" << std::endl;
        std::cout << "Total: " << totalTests << " tests, " << passedTests << " passed, "
                  << failedTests << " failed" << std::endl;
        return 1;
    }

    check("hasCorrectionCurve() after computation", processor.hasCorrectionCurve());

    // --- Validate Correction Curve Shape ---
    std::cout << std::endl << "--- Correction Curve Validation ---" << std::endl;

    std::array<float, EQMatchProcessor::NUM_BINS> correctionDB{};
    processor.getCorrectionCurveDB(correctionDB);

    float corrLow = averageDBInRange(correctionDB, sampleRate, 100.0f, 500.0f);
    float corrHigh = averageDBInRange(correctionDB, sampleRate, 2000.0f, 8000.0f);

    std::cout << "  Correction curve: low avg=" << corrLow << " dB, high avg=" << corrHigh << " dB" << std::endl;

    // The correction should boost below 1 kHz (to compensate the -6 dB shelf)
    check("Correction boosts low frequencies (avg > +2 dB in 100-500 Hz)",
          corrLow > 2.0f);

    // Above the shelf frequency, correction should be near flat
    check("Correction near flat above shelf (|avg| < 2 dB in 2-8 kHz)",
          std::abs(corrHigh) < 2.0f);

    // --- Validate IR Properties ---
    std::cout << std::endl << "--- IR Validation ---" << std::endl;

    juce::AudioBuffer<float> ir = processor.getCorrectionIR();
    check("IR has samples", ir.getNumSamples() > 0);

    if (ir.getNumSamples() > 0)
    {
        float irPeak = ir.getMagnitude(0, ir.getNumSamples());
        bool hasNaN = false;
        const float* irData = ir.getReadPointer(0);
        for (int i = 0; i < ir.getNumSamples(); ++i)
        {
            if (std::isnan(irData[i]) || std::isinf(irData[i]))
            {
                hasNaN = true;
                break;
            }
        }

        std::cout << "  IR samples: " << ir.getNumSamples() << ", peak: " << irPeak << std::endl;
        check("IR has no NaN/Inf values", !hasNaN);
        check("IR peak amplitude > 0.001", irPeak > 0.001f);
        check("IR peak amplitude < 10.0", irPeak < 10.0f);

        // Check that minimum-phase IR has energy concentrated at the start
        float firstQuarterEnergy = 0.0f;
        float totalEnergy = 0.0f;
        int quarter = ir.getNumSamples() / 4;
        for (int i = 0; i < ir.getNumSamples(); ++i)
        {
            float e = irData[i] * irData[i];
            totalEnergy += e;
            if (i < quarter)
                firstQuarterEnergy += e;
        }
        float energyRatio = (totalEnergy > 0.0f) ? (firstQuarterEnergy / totalEnergy) : 0.0f;
        std::cout << "  Energy in first quarter: " << (energyRatio * 100.0f) << "%" << std::endl;
        check("Minimum-phase: >50% energy in first quarter of IR", energyRatio > 0.5f);
    }

    // --- Convolution Validation ---
    std::cout << std::endl << "--- Convolution Validation ---" << std::endl;

    if (ir.getNumSamples() > 0)
    {
        const float* irData = ir.getReadPointer(0);
        int irLen = ir.getNumSamples();

        // Convolve current signal with correction IR
        int outLen = signalLength; // Keep same length, just discard convolution tail
        std::vector<float> corrected(static_cast<size_t>(outLen));
        std::cout << "  Convolving " << signalLength << " samples with " << irLen
                  << "-tap FIR..." << std::endl;
        convolve(currentSignal.data(), signalLength, irData, irLen, corrected.data(), outLen);

        // Measure corrected spectrum (skip first irLen samples for settling)
        int analysisStart = irLen;
        int analysisLen = outLen - analysisStart;
        if (analysisLen >= EQMatchProcessor::FFT_SIZE)
        {
            std::array<float, EQMatchProcessor::NUM_BINS> correctedSpectrum{};
            measureSpectrum(corrected.data() + analysisStart, analysisLen, correctedSpectrum);

            // Also measure reference over the same region for fair comparison
            std::array<float, EQMatchProcessor::NUM_BINS> refAnalysis{};
            measureSpectrum(referenceSignal.data() + analysisStart, analysisLen, refAnalysis);

            // Compare corrected vs reference in audible range
            float maxDiffLow = 0.0f, maxDiffMid = 0.0f, maxDiffHigh = 0.0f;
            float avgDiffLow = 0.0f, avgDiffMid = 0.0f, avgDiffHigh = 0.0f;
            int countLow = 0, countMid = 0, countHigh = 0;

            float nyquist = static_cast<float>(sampleRate * 0.5);
            float binWidth = nyquist / static_cast<float>(EQMatchProcessor::NUM_BINS - 1);

            for (int k = 1; k < EQMatchProcessor::NUM_BINS; ++k)
            {
                float freq = static_cast<float>(k) * binWidth;
                float diff = std::abs(correctedSpectrum[static_cast<size_t>(k)]
                                      - refAnalysis[static_cast<size_t>(k)]);

                if (freq >= 100.0f && freq < 500.0f)
                {
                    maxDiffLow = std::max(maxDiffLow, diff);
                    avgDiffLow += diff;
                    countLow++;
                }
                else if (freq >= 500.0f && freq < 4000.0f)
                {
                    maxDiffMid = std::max(maxDiffMid, diff);
                    avgDiffMid += diff;
                    countMid++;
                }
                else if (freq >= 4000.0f && freq <= 10000.0f)
                {
                    maxDiffHigh = std::max(maxDiffHigh, diff);
                    avgDiffHigh += diff;
                    countHigh++;
                }
            }

            if (countLow > 0) avgDiffLow /= static_cast<float>(countLow);
            if (countMid > 0) avgDiffMid /= static_cast<float>(countMid);
            if (countHigh > 0) avgDiffHigh /= static_cast<float>(countHigh);

            std::cout << "  Spectral match (corrected vs reference):" << std::endl;
            std::cout << "    100-500 Hz:   avg diff=" << avgDiffLow << " dB, max=" << maxDiffLow << " dB" << std::endl;
            std::cout << "    500-4000 Hz:  avg diff=" << avgDiffMid << " dB, max=" << maxDiffMid << " dB" << std::endl;
            std::cout << "    4000-10000 Hz: avg diff=" << avgDiffHigh << " dB, max=" << maxDiffHigh << " dB" << std::endl;

            check("Corrected spectrum matches reference avg <3 dB (100-500 Hz)", avgDiffLow < 3.0f);
            check("Corrected spectrum matches reference avg <3 dB (500-4000 Hz)", avgDiffMid < 3.0f);
            check("Corrected spectrum matches reference avg <3 dB (4000-10000 Hz)", avgDiffHigh < 3.0f);
        }
        else
        {
            std::cout << "  WARNING: Not enough samples after settling for convolution analysis"
                      << " (need " << EQMatchProcessor::FFT_SIZE << ", have " << analysisLen << ")"
                      << std::endl;
        }
    }

    // --- Summary ---
    std::cout << std::endl << "=== Summary ===" << std::endl;
    std::cout << "Total: " << totalTests << " tests, " << passedTests << " passed, "
              << failedTests << " failed" << std::endl;

    if (failedTests > 0)
    {
        std::cout << "*** FAIL ***" << std::endl;
        return 1;
    }

    std::cout << "*** ALL TESTS PASSED ***" << std::endl;
    return 0;
}

#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

//==============================================================================
/**
    EQMatchProcessor — Logic Pro Match EQ style spectrum matching.

    Workflow:
    1. startLearningCurrent/Reference() — begin accumulating FFT frames (Welch's method)
    2. feedLearningBlock() — called from processBlock with mono audio
    3. stopLearning() — stop accumulation
    4. computeCorrection() — compute smoothed FIR from difference curve
    5. getCorrectionIR() — get the FIR as AudioBuffer for juce::dsp::Convolution
*/
class EQMatchProcessor
{
public:
    static constexpr int FFT_ORDER = 12;              // 4096-point FFT
    static constexpr int FFT_SIZE = 1 << FFT_ORDER;   // 4096
    static constexpr int NUM_BINS = FFT_SIZE / 2 + 1; // 2049 frequency bins
    static constexpr int FIR_LENGTH = 4096;            // Output FIR length
    static constexpr int HOP_SIZE = FFT_SIZE / 2;      // 50% overlap for Welch's method

    // Learned spectrum via Welch's method (averaged periodogram)
    struct LearnedSpectrum
    {
        std::array<double, NUM_BINS> powerSum{};  // Running sum of |X[k]|^2
        int frameCount = 0;
        bool valid = false;

        void reset()
        {
            powerSum.fill(0.0);
            frameCount = 0;
            valid = false;
        }

        void addFrame(const float* fftData, int fftSize)
        {
            // fftData is interleaved real/imag from juce::dsp::FFT
            int bins = fftSize / 2 + 1;
            for (int k = 0; k < bins && k < NUM_BINS; ++k)
            {
                float re = fftData[k * 2];
                float im = fftData[k * 2 + 1];
                powerSum[static_cast<size_t>(k)] += static_cast<double>(re * re + im * im);
            }
            frameCount++;
            if (frameCount >= 3)  // Need at least a few frames
                valid = true;
        }

        void getAverageMagnitudeDB(std::array<float, NUM_BINS>& outDB) const
        {
            if (frameCount <= 0)
            {
                outDB.fill(-100.0f);
                return;
            }
            double invCount = 1.0 / static_cast<double>(frameCount);
            for (int k = 0; k < NUM_BINS; ++k)
            {
                double avgPower = powerSum[static_cast<size_t>(k)] * invCount;
                double mag = std::sqrt(avgPower + 1e-30);
                outDB[static_cast<size_t>(k)] = static_cast<float>(20.0 * std::log10(mag + 1e-30));
            }
        }
    };

    EQMatchProcessor() = default;

    void prepare(double sr, int /*maxBlockSize*/)
    {
        sampleRate = sr;
        // Pre-allocate all buffers
        learningFFTBuffer.resize(static_cast<size_t>(FFT_SIZE * 2), 0.0f);
        learningInputBuffer.resize(static_cast<size_t>(FFT_SIZE), 0.0f);
        learningInputPos = 0;
        samplesSinceLastFrame = 0;
        correctionFIR.resize(static_cast<size_t>(FIR_LENGTH), 0.0f);
    }

    void reset()
    {
        stopLearning();
        currentSpectrum.reset();
        referenceSpectrum.reset();
        correctionValid = false;
        correctionCurveDB.fill(0.0f);
    }

    // --- Learning API (thread-safe: start/stop from UI, feed from audio thread) ---

    void startLearningCurrent()
    {
        {
            std::lock_guard<std::mutex> lock(spectrumMutex);
            currentSpectrum.reset();
        }
        learningInputPos = 0;
        samplesSinceLastFrame = -(FFT_SIZE - HOP_SIZE);  // Require full window before first frame
        learningTarget.store(LearningTarget::Current, std::memory_order_release);
    }

    void startLearningReference()
    {
        {
            std::lock_guard<std::mutex> lock(spectrumMutex);
            referenceSpectrum.reset();
        }
        learningInputPos = 0;
        samplesSinceLastFrame = -(FFT_SIZE - HOP_SIZE);  // Require full window before first frame
        learningTarget.store(LearningTarget::Reference, std::memory_order_release);
    }

    void stopLearning()
    {
        learningTarget.store(LearningTarget::None, std::memory_order_release);
    }

    /** Feed mono audio samples for learning. Called from audio thread. */
    void feedLearningBlock(const float* monoSamples, int numSamples)
    {
        auto target = learningTarget.load(std::memory_order_acquire);
        if (target == LearningTarget::None)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            learningInputBuffer[static_cast<size_t>(learningInputPos)] = monoSamples[i];
            learningInputPos++;

            if (learningInputPos >= FFT_SIZE)
                learningInputPos = 0;  // Wrap around (circular buffer)

            samplesSinceLastFrame++;

            // Process a frame every HOP_SIZE samples (50% overlap)
            if (samplesSinceLastFrame >= HOP_SIZE && getCurrentFrameCount() >= 0)
            {
                samplesSinceLastFrame = 0;
                processLearningFrame(target);
            }
        }
    }

    bool isLearning() const
    {
        return learningTarget.load(std::memory_order_acquire) != LearningTarget::None;
    }

    bool isLearningCurrent() const
    {
        return learningTarget.load(std::memory_order_acquire) == LearningTarget::Current;
    }

    bool isLearningReference() const
    {
        return learningTarget.load(std::memory_order_acquire) == LearningTarget::Reference;
    }

    int getLearningFrameCount() const
    {
        auto target = learningTarget.load(std::memory_order_acquire);
        if (target == LearningTarget::Current)
            return currentSpectrum.frameCount;
        if (target == LearningTarget::Reference)
            return referenceSpectrum.frameCount;
        // Return whichever was most recently active
        return std::max(currentSpectrum.frameCount, referenceSpectrum.frameCount);
    }

    // --- Spectrum state queries ---

    bool hasCurrentSpectrum() const { return currentSpectrum.valid; }
    bool hasReferenceSpectrum() const { return referenceSpectrum.valid; }

    void getCurrentSpectrumDB(std::array<float, NUM_BINS>& outDB) const
    {
        std::lock_guard<std::mutex> lock(spectrumMutex);
        currentSpectrum.getAverageMagnitudeDB(outDB);
    }

    void getReferenceSpectrumDB(std::array<float, NUM_BINS>& outDB) const
    {
        std::lock_guard<std::mutex> lock(spectrumMutex);
        referenceSpectrum.getAverageMagnitudeDB(outDB);
    }

    // --- Correction computation (call from UI thread after learning stops) ---

    bool computeCorrection(float smoothingSemitones, float applyAmount,
                           float limitBoostDB, float limitCutDB,
                           bool minimumPhase)
    {
        DBG("EQMatchProcessor::computeCorrection - current valid=" + juce::String(currentSpectrum.valid ? 1 : 0)
            + " frames=" + juce::String(currentSpectrum.frameCount)
            + ", ref valid=" + juce::String(referenceSpectrum.valid ? 1 : 0)
            + " frames=" + juce::String(referenceSpectrum.frameCount));

        if (!currentSpectrum.valid || !referenceSpectrum.valid)
        {
            DBG("  FAILED: spectra not valid");
            return false;
        }

        std::array<float, NUM_BINS> currentDB{};
        std::array<float, NUM_BINS> referenceDB{};

        {
            std::lock_guard<std::mutex> lock(spectrumMutex);
            currentSpectrum.getAverageMagnitudeDB(currentDB);
            referenceSpectrum.getAverageMagnitudeDB(referenceDB);
        }

        // Compute difference curve: reference - current (what we need to add)
        std::array<float, NUM_BINS> diffCurve{};
        double diffSum = 0.0;
        for (int k = 0; k < NUM_BINS; ++k)
        {
            diffCurve[static_cast<size_t>(k)] = referenceDB[static_cast<size_t>(k)]
                                                - currentDB[static_cast<size_t>(k)];
            diffSum += diffCurve[static_cast<size_t>(k)];
        }

        // Normalize: remove overall level difference (center on 0 dB)
        float dcOffset = static_cast<float>(diffSum / NUM_BINS);
        for (int k = 0; k < NUM_BINS; ++k)
            diffCurve[static_cast<size_t>(k)] -= dcOffset;

        // Clamp extreme dB values before smoothing to prevent outliers from dominating
        for (int k = 0; k < NUM_BINS; ++k)
            diffCurve[static_cast<size_t>(k)] = juce::jlimit(-30.0f, 30.0f, diffCurve[static_cast<size_t>(k)]);

        // Re-center after clamp so the curve remains zero-mean
        {
            double clampSum = 0.0;
            for (int k = 0; k < NUM_BINS; ++k)
                clampSum += diffCurve[static_cast<size_t>(k)];
            float clampOffset = static_cast<float>(clampSum / NUM_BINS);
            for (int k = 0; k < NUM_BINS; ++k)
                diffCurve[static_cast<size_t>(k)] -= clampOffset;
        }

        // Apply smoothing
        if (smoothingSemitones > 0.0f)
            applyFractionalOctaveSmoothing(diffCurve, smoothingSemitones);

        // Log curve stats for diagnostics
        {
            float minDB = 999.0f, maxDB = -999.0f;
            for (int k = 0; k < NUM_BINS; ++k)
            {
                minDB = std::min(minDB, diffCurve[static_cast<size_t>(k)]);
                maxDB = std::max(maxDB, diffCurve[static_cast<size_t>(k)]);
            }
            DBG("  Correction curve: min=" + juce::String(minDB, 1) + " max=" + juce::String(maxDB, 1)
                + " dcOffset=" + juce::String(dcOffset, 1) + " apply=" + juce::String(applyAmount, 2));
        }

        // Scale by apply amount (-1.0 to +1.0)
        for (int k = 0; k < NUM_BINS; ++k)
            diffCurve[static_cast<size_t>(k)] *= applyAmount;

        // Apply user-specified limit boost / limit cut (tighter than default)
        if (limitBoostDB > 0.0f)
        {
            for (int k = 0; k < NUM_BINS; ++k)
                diffCurve[static_cast<size_t>(k)] = std::min(diffCurve[static_cast<size_t>(k)], limitBoostDB);
        }
        if (limitCutDB < 0.0f)
        {
            for (int k = 0; k < NUM_BINS; ++k)
                diffCurve[static_cast<size_t>(k)] = std::max(diffCurve[static_cast<size_t>(k)], limitCutDB);
        }

        // Hard safety limit: ±15 dB max correction (like Logic Pro Match EQ)
        static constexpr float HARD_LIMIT_DB = 15.0f;
        for (int k = 0; k < NUM_BINS; ++k)
            diffCurve[static_cast<size_t>(k)] = juce::jlimit(-HARD_LIMIT_DB, HARD_LIMIT_DB, diffCurve[static_cast<size_t>(k)]);

        // Re-center after hard limit so FIR generation receives a DC-neutral curve,
        // then re-clamp to ensure limits are still respected after the shift
        {
            double hardSum = 0.0;
            for (int k = 0; k < NUM_BINS; ++k)
                hardSum += diffCurve[static_cast<size_t>(k)];
            float hardOffset = static_cast<float>(hardSum / NUM_BINS);
            for (int k = 0; k < NUM_BINS; ++k)
            {
                diffCurve[static_cast<size_t>(k)] -= hardOffset;
                diffCurve[static_cast<size_t>(k)] = juce::jlimit(-HARD_LIMIT_DB, HARD_LIMIT_DB, diffCurve[static_cast<size_t>(k)]);
            }
        }

        // Store correction curve for display
        {
            std::lock_guard<std::mutex> lock(correctionMutex);
            correctionCurveDB = diffCurve;
        }

        // Generate FIR from the correction curve
        if (minimumPhase)
            generateMinimumPhaseFIR(diffCurve);
        else
            generateLinearPhaseFIR(diffCurve);

        // Validate the FIR — check for NaN/Inf and log stats
        {
            std::lock_guard<std::mutex> lock(correctionMutex);
            float peak = 0.0f;
            bool hasNaN = false;
            for (int i = 0; i < FIR_LENGTH; ++i)
            {
                float v = correctionFIR[static_cast<size_t>(i)];
                if (std::isnan(v) || std::isinf(v))
                {
                    correctionFIR[static_cast<size_t>(i)] = 0.0f;
                    hasNaN = true;
                }
                peak = std::max(peak, std::abs(v));
            }
            DBG("  FIR generated: peak=" + juce::String(peak, 6) + " hasNaN=" + juce::String(hasNaN ? 1 : 0)
                + " phase=" + juce::String(minimumPhase ? "minimum" : "linear"));
        }

        correctionValid = true;
        return true;
    }

    /** Get the correction FIR as a JUCE AudioBuffer (mono). */
    juce::AudioBuffer<float> getCorrectionIR() const
    {
        if (!correctionValid)
            return {};

        juce::AudioBuffer<float> ir(1, FIR_LENGTH);
        std::lock_guard<std::mutex> lock(correctionMutex);
        std::copy(correctionFIR.begin(),
                  correctionFIR.begin() + FIR_LENGTH,
                  ir.getWritePointer(0));
        return ir;
    }

    /** Get the smoothed correction curve in dB for UI display. */
    void getCorrectionCurveDB(std::array<float, NUM_BINS>& outDB) const
    {
        std::lock_guard<std::mutex> lock(correctionMutex);
        outDB = correctionCurveDB;
    }

    bool hasCorrectionCurve() const { return correctionValid; }

    void clearAll()
    {
        stopLearning();
        {
            std::lock_guard<std::mutex> lock(spectrumMutex);
            currentSpectrum.reset();
            referenceSpectrum.reset();
        }
        {
            std::lock_guard<std::mutex> lock(correctionMutex);
            correctionValid = false;
            correctionCurveDB.fill(0.0f);
        }
    }

    double getSampleRate() const { return sampleRate; }

private:
    double sampleRate = 44100.0;

    // Learning state
    enum class LearningTarget { None, Current, Reference };
    std::atomic<LearningTarget> learningTarget{LearningTarget::None};

    LearnedSpectrum currentSpectrum;
    LearnedSpectrum referenceSpectrum;

    // Learning FFT
    juce::dsp::FFT learningFFT{FFT_ORDER};
    juce::dsp::WindowingFunction<float> hannWindow{
        static_cast<size_t>(FFT_SIZE), juce::dsp::WindowingFunction<float>::hann};

    std::vector<float> learningInputBuffer;   // Circular input buffer (FFT_SIZE)
    int learningInputPos = 0;
    int samplesSinceLastFrame = 0;
    std::vector<float> learningFFTBuffer;     // Scratch for FFT (2 * FFT_SIZE)

    // Correction state
    std::array<float, NUM_BINS> correctionCurveDB{};
    std::vector<float> correctionFIR;
    bool correctionValid = false;

    mutable std::mutex spectrumMutex;     // Protects learned spectra
    mutable std::mutex correctionMutex;   // Protects correction curve and FIR

    int getCurrentFrameCount() const
    {
        auto target = learningTarget.load(std::memory_order_relaxed);
        if (target == LearningTarget::Current) return currentSpectrum.frameCount;
        if (target == LearningTarget::Reference) return referenceSpectrum.frameCount;
        return -1;
    }

    /** Process one FFT frame from the circular input buffer. */
    void processLearningFrame(LearningTarget target)
    {
        // Gather FFT_SIZE samples from circular buffer (current position is the end)
        int readPos = learningInputPos;  // This is where the next sample would go
        for (int i = 0; i < FFT_SIZE; ++i)
        {
            int srcIdx = (readPos + i) % FFT_SIZE;
            learningFFTBuffer[static_cast<size_t>(i)] = learningInputBuffer[static_cast<size_t>(srcIdx)];
        }

        // Zero the imaginary part
        for (int i = FFT_SIZE; i < FFT_SIZE * 2; ++i)
            learningFFTBuffer[static_cast<size_t>(i)] = 0.0f;

        // Apply Hann window
        hannWindow.multiplyWithWindowingTable(learningFFTBuffer.data(),
                                              static_cast<size_t>(FFT_SIZE));

        // Forward FFT (in-place, interleaved complex output)
        learningFFT.performRealOnlyForwardTransform(learningFFTBuffer.data());

        // Accumulate into the target spectrum (locked to synchronize with UI reads)
        {
            std::lock_guard<std::mutex> lock(spectrumMutex);
            LearnedSpectrum& spectrum = (target == LearningTarget::Current)
                                         ? currentSpectrum : referenceSpectrum;
            spectrum.addFrame(learningFFTBuffer.data(), FFT_SIZE);
        }
    }

    /** Fractional-octave smoothing on a log-frequency scale. */
    void applyFractionalOctaveSmoothing(std::array<float, NUM_BINS>& curve,
                                        float semitones) const
    {
        float octaves = semitones / 12.0f;
        float halfOctaves = octaves / 2.0f;
        float nyquist = static_cast<float>(sampleRate * 0.5);
        float binWidth = nyquist / static_cast<float>(NUM_BINS - 1);

        std::array<float, NUM_BINS> smoothed{};

        for (int k = 1; k < NUM_BINS; ++k)
        {
            float centerFreq = static_cast<float>(k) * binWidth;
            float lowerFreq = centerFreq * std::pow(2.0f, -halfOctaves);
            float upperFreq = centerFreq * std::pow(2.0f, halfOctaves);

            int lowerBin = std::max(0, static_cast<int>(lowerFreq / binWidth));
            int upperBin = std::min(NUM_BINS - 1, static_cast<int>(upperFreq / binWidth));

            float sum = 0.0f;
            int count = 0;
            for (int j = lowerBin; j <= upperBin; ++j)
            {
                sum += curve[static_cast<size_t>(j)];
                count++;
            }

            smoothed[static_cast<size_t>(k)] = (count > 0) ? (sum / static_cast<float>(count)) : curve[static_cast<size_t>(k)];
        }

        // DC bin: just use nearest neighbors
        smoothed[0] = smoothed[1];
        curve = smoothed;
    }

    /** Generate a linear-phase (symmetric) FIR from a dB magnitude curve. */
    void generateLinearPhaseFIR(const std::array<float, NUM_BINS>& curveDB)
    {
        // We need a larger FFT for the FIR generation
        static constexpr int GEN_FFT_ORDER = 12;  // Same as FFT_ORDER
        static constexpr int GEN_FFT_SIZE = 1 << GEN_FFT_ORDER;
        juce::dsp::FFT genFFT(GEN_FFT_ORDER);

        std::vector<float> fftBuf(static_cast<size_t>(GEN_FFT_SIZE * 2), 0.0f);

        // Set magnitude spectrum with zero phase (linear phase = symmetric FIR)
        int genBins = GEN_FFT_SIZE / 2 + 1;
        for (int k = 0; k < genBins; ++k)
        {
            // Map from our NUM_BINS to genBins
            int srcBin = (k * (NUM_BINS - 1)) / (genBins - 1);
            srcBin = std::min(srcBin, NUM_BINS - 1);

            float dB = curveDB[static_cast<size_t>(srcBin)];
            float linearMag = std::pow(10.0f, dB / 20.0f);

            fftBuf[static_cast<size_t>(k * 2)] = linearMag;      // Real
            fftBuf[static_cast<size_t>(k * 2 + 1)] = 0.0f;       // Imag (zero phase)
        }

        // IFFT to get zero-phase impulse response
        genFFT.performRealOnlyInverseTransform(fftBuf.data());

        // Circular shift to center the impulse (make it causal)
        int halfLen = GEN_FFT_SIZE / 2;
        std::vector<float> shifted(static_cast<size_t>(GEN_FFT_SIZE));
        for (int i = 0; i < GEN_FFT_SIZE; ++i)
        {
            int srcIdx = (i + halfLen) % GEN_FFT_SIZE;
            shifted[static_cast<size_t>(i)] = fftBuf[static_cast<size_t>(srcIdx)];
        }

        // Apply Kaiser window (beta=8 for good sidelobe suppression)
        juce::dsp::WindowingFunction<float> kaiserWindow(
            static_cast<size_t>(FIR_LENGTH),
            juce::dsp::WindowingFunction<float>::kaiser, true, 8.0f);

        // Truncate to FIR_LENGTH and window
        int startOffset = (GEN_FFT_SIZE - FIR_LENGTH) / 2;
        {
            std::lock_guard<std::mutex> lock(correctionMutex);
            for (int i = 0; i < FIR_LENGTH; ++i)
            {
                int srcIdx = startOffset + i;
                correctionFIR[static_cast<size_t>(i)] =
                    (srcIdx >= 0 && srcIdx < GEN_FFT_SIZE) ? shifted[static_cast<size_t>(srcIdx)] : 0.0f;
            }
            kaiserWindow.multiplyWithWindowingTable(correctionFIR.data(),
                                                     static_cast<size_t>(FIR_LENGTH));
        }
    }

    /** Generate a minimum-phase FIR from a dB magnitude curve using the cepstral method. */
    void generateMinimumPhaseFIR(const std::array<float, NUM_BINS>& curveDB)
    {
        // Use a larger FFT for better frequency resolution in the cepstral domain
        static constexpr int GEN_FFT_ORDER = 13;  // 8192 for better cepstral precision
        static constexpr int GEN_FFT_SIZE = 1 << GEN_FFT_ORDER;
        static constexpr int GEN_BINS = GEN_FFT_SIZE / 2 + 1;

        juce::dsp::FFT genFFT(GEN_FFT_ORDER);
        std::vector<float> buf(static_cast<size_t>(GEN_FFT_SIZE * 2), 0.0f);

        // Step 1: Create log-magnitude spectrum
        for (int k = 0; k < GEN_BINS; ++k)
        {
            // Map from NUM_BINS to GEN_BINS
            float frac = static_cast<float>(k) / static_cast<float>(GEN_BINS - 1);
            int srcBin = static_cast<int>(frac * static_cast<float>(NUM_BINS - 1));
            srcBin = std::min(srcBin, NUM_BINS - 1);

            float dB = curveDB[static_cast<size_t>(srcBin)];
            float linearMag = std::pow(10.0f, dB / 20.0f);
            linearMag = std::max(linearMag, 1e-10f);  // Prevent log(0)

            float logMag = std::log(linearMag);

            // Store log magnitude as real, zero imag
            buf[static_cast<size_t>(k * 2)] = logMag;
            buf[static_cast<size_t>(k * 2 + 1)] = 0.0f;
        }

        // Step 2: IFFT to get real cepstrum
        genFFT.performRealOnlyInverseTransform(buf.data());

        // Step 3: Apply causal window to cepstrum
        // buf now contains the cepstrum (real-valued, length GEN_FFT_SIZE)
        // Keep DC as-is, double positive quefrencies, zero negative quefrencies
        // buf[0] stays (DC)
        for (int n = 1; n < GEN_FFT_SIZE / 2; ++n)
            buf[static_cast<size_t>(n)] *= 2.0f;           // Positive quefrencies: double
        // buf[GEN_FFT_SIZE/2] stays (Nyquist)
        for (int n = GEN_FFT_SIZE / 2 + 1; n < GEN_FFT_SIZE; ++n)
            buf[static_cast<size_t>(n)] = 0.0f;             // Negative quefrencies: zero

        // Step 4: FFT back to get complex log spectrum with minimum-phase angle
        // We need to use the full complex FFT here. Since performRealOnlyForwardTransform
        // expects real input and produces interleaved output, we can use it.
        genFFT.performRealOnlyForwardTransform(buf.data());

        // Step 5: Exponentiate to get complex H_min(f)
        // Clamp re to prevent exp() overflow (±20 → gain range ±174 dB, more than enough)
        for (int k = 0; k < GEN_BINS; ++k)
        {
            float re = juce::jlimit(-20.0f, 20.0f, buf[static_cast<size_t>(k * 2)]);
            float im = buf[static_cast<size_t>(k * 2 + 1)];
            float expRe = std::exp(re);
            buf[static_cast<size_t>(k * 2)]     = expRe * std::cos(im);
            buf[static_cast<size_t>(k * 2 + 1)] = expRe * std::sin(im);
        }

        // Step 6: IFFT to get minimum-phase impulse response
        genFFT.performRealOnlyInverseTransform(buf.data());

        // Step 7: Truncate to FIR_LENGTH and apply a fade-out tail window.
        // For minimum-phase FIR, energy is concentrated at the start (sample 0),
        // so we must NOT use a symmetric window that zeros out the beginning.
        // Instead, apply a half-Hann fade-out only to the tail to prevent truncation artifacts.
        {
            std::lock_guard<std::mutex> lock(correctionMutex);
            for (int i = 0; i < FIR_LENGTH; ++i)
                correctionFIR[static_cast<size_t>(i)] = (i < GEN_FFT_SIZE) ? buf[static_cast<size_t>(i)] : 0.0f;

            // Fade out the last 25% using a half-Hann window
            int fadeLen = FIR_LENGTH / 4;
            int fadeStart = FIR_LENGTH - fadeLen;
            for (int i = 0; i < fadeLen; ++i)
            {
                float t = static_cast<float>(i) / static_cast<float>(fadeLen);
                float window = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * t));
                correctionFIR[static_cast<size_t>(fadeStart + i)] *= window;
            }
        }
    }
};

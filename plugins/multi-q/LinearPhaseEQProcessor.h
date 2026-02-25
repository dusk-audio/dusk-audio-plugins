#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

/**
    Linear Phase EQ Processor

    Uses FIR filtering via FFT convolution to achieve linear phase response.
    This eliminates phase distortion but introduces latency.

    How it works:
    1. Build the desired magnitude response from the EQ parameters
    2. Create a symmetric FIR filter via inverse FFT (linear phase = symmetric impulse response)
    3. Apply the filter using overlap-add FFT convolution

    Latency: FIR_LENGTH / 2 samples (half the filter length)

    Filter lengths:
    - 4096: ~46ms @ 44.1kHz, good frequency resolution
    - 8192: ~93ms @ 44.1kHz, excellent frequency resolution (default)
    - 16384: ~186ms @ 44.1kHz, mastering-grade resolution

    Thread Safety:
    - IR generation happens on a background thread
    - Audio thread only reads the ready IR buffer via atomic pointer swap
    - ProcessorState swap ensures glitch-free filter length changes without audio-thread allocations
*/
class LinearPhaseEQProcessor
{
public:
    // Filter length options (power of 2)
    enum FilterLength
    {
        Short = 4096,   // Lower latency, good for mixing
        Medium = 8192,  // Balanced (default)
        Long = 16384    // Highest quality, mastering
    };

    LinearPhaseEQProcessor()
    {
        // This prevents race conditions where resize() could be called while audio thread reads
        // Max filter length = Long (16384), convolution FFT size = 2 * Long = 32768
        // Buffer needs 2 * convolution FFT size for complex frequency domain data
        static constexpr size_t maxConvFftSize = static_cast<size_t>(Long) * 2;  // 32768
        static constexpr size_t maxIrBufferSize = maxConvFftSize * 2;            // 65536
        irBufferA = std::make_unique<std::vector<float>>(maxIrBufferSize, 0.0f);
        irBufferB = std::make_unique<std::vector<float>>(maxIrBufferSize, 0.0f);

        initializeFlatIR(irBufferA.get(), Medium);  // Default filter length
        initializeFlatIR(irBufferB.get(), Medium);  // Both buffers start with flat IR

        irActivePtr.store(irBufferA.get(), std::memory_order_release);
        irReadyPtr.store(irBufferB.get(), std::memory_order_release);

        // Pre-allocate per-channel crossfade buffers to maximum hop size (Long / 2)
        for (auto& fade : crossfadeState)
            fade.buffer.resize(static_cast<size_t>(Long / 2), 0.0f);

        activeState = std::make_unique<ProcessorState>(Medium);
        readyState = std::make_unique<ProcessorState>(Medium);

        startBackgroundThread();
    }

    ~LinearPhaseEQProcessor()
    {
        stopBackgroundThread();
    }

    void prepare(double sampleRate, int newMaxBlockSize)
    {
        currentSampleRate.store(sampleRate, std::memory_order_release);
        maxBlockSize = newMaxBlockSize;
        reset();
        // Mark for IR rebuild on background thread
        irNeedsUpdate.store(true);
        backgroundCV.notify_one();
    }

    /** Reset all processing state.
        Must only be called when audio processing is stopped (e.g., from prepareToPlay
        or releaseResources). JUCE guarantees these are never called concurrently with
        processBlock, so no additional synchronization is needed. */
    void reset()
    {
        auto* state = activeState.get();
        if (!state) return;
        state->reset();
    }

    void setFilterLength(FilterLength length)
    {
        int newSize = static_cast<int>(length);
        int currentPending = pendingFftSize.load(std::memory_order_acquire);
        if (newSize != currentPending)
        {
            pendingFftSize.store(newSize, std::memory_order_release);
            filterLengthChanged.store(true, std::memory_order_release);
            irNeedsUpdate.store(true);
            backgroundCV.notify_one();
        }
    }

    int getFilterLength() const
    {
        return currentFilterLength.load(std::memory_order_relaxed);
    }

    /**
        Returns the latency in samples.
        Linear phase EQ has inherent latency of filterLength/2 samples.
    */
    int getLatencyInSamples() const
    {
        return currentFilterLength.load(std::memory_order_relaxed) / 2;
    }

    /**
        Update the impulse response with new EQ parameters.
        Call this when EQ band parameters change.
        This is safe to call from any thread - actual IR rebuild happens on background thread.
    */
    void updateImpulseResponse(
        const std::array<bool, 8>& newBandEnabled,
        const std::array<float, 8>& newBandFreq,
        const std::array<float, 8>& newBandGain,
        const std::array<float, 8>& newBandQ,
        const std::array<int, 2>& newBandSlope,
        float newMasterGain)
    {
        // Store parameters atomically (using mutex for the arrays)
        {
            std::lock_guard<std::mutex> lock(paramMutex);
            pendingBandEnabled = newBandEnabled;
            pendingBandFreq = newBandFreq;
            pendingBandGain = newBandGain;
            pendingBandQ = newBandQ;
            pendingBandSlope = newBandSlope;
            pendingMasterGain = newMasterGain;
        }
        irNeedsUpdate.store(true);
        backgroundCV.notify_one();
    }

    /** Process a single channel of audio using overlap-add FFT convolution */
    void processChannel(int channel, float* channelData, int numSamples)
    {
        auto* state = activeState.get();
        if (!state || !state->fft || channel < 0 || channel >= MAX_CHANNELS)
            return;

        auto* irBuffer = irActivePtr.load(std::memory_order_acquire);
        if (!irBuffer || irBuffer->size() < static_cast<size_t>(state->fftSize * 2))
            return;

        auto& ch = state->channels[static_cast<size_t>(channel)];
        auto& fade = crossfadeState[static_cast<size_t>(channel)];

        for (int i = 0; i < numSamples; ++i)
        {
            ch.inputAccum[static_cast<size_t>(ch.inputWritePos)] = channelData[i];
            ch.inputWritePos = (ch.inputWritePos + 1) % state->filterLength;
            ch.samplesInInputBuffer++;

            if (ch.samplesInInputBuffer >= state->hopSize)
            {
                processFFTBlock(state, &ch, irBuffer);
                ch.samplesInInputBuffer = 0;
            }

            float output = ch.latencyDelay[static_cast<size_t>(ch.delayReadPos)];
            ch.delayReadPos = (ch.delayReadPos + 1) % (state->fftSize * 2);

            // Delayed crossfade: wait for new-IR samples to reach the read position
            if (fade.delay > 0)
            {
                fade.delay--;
                if (fade.delay == 0)
                    fade.remaining = fade.length;
            }
            else if (fade.remaining > 0)
            {
                float t = static_cast<float>(fade.remaining) / static_cast<float>(fade.length);
                int fadeIdx = fade.length - fade.remaining;
                float oldSample = fade.buffer[static_cast<size_t>(fadeIdx)];
                output = output * (1.0f - t) + oldSample * t;
                fade.remaining--;
            }

            channelData[i] = output;
        }
    }

    /** Process stereo audio */
    void processStereo(juce::AudioBuffer<float>& buffer)
    {
        juce::ScopedNoDenormals noDenormals;

        // Atomically check if state+IR or just IR needs swapping
        int swap = swapReady.load(std::memory_order_acquire);
        if (swap != SwapNone)
        {
            // Swap ProcessorState FIRST if filter length changed
            if (swap == SwapStateAndIR)
            {
                std::swap(activeState, readyState);
                for (auto& fade : crossfadeState)
                {
                    fade.remaining = 0;
                    fade.delay = 0;
                }
                if (activeState)
                    currentFilterLength.store(activeState->filterLength, std::memory_order_relaxed);
            }

            // Swap IR buffer
            auto* state = activeState.get();
            if (state)
            {
                // Snapshot per-channel crossfade buffers before swapping IR
                int fadeLen = juce::jmin(state->hopSize, static_cast<int>(crossfadeState[0].buffer.size()));
                for (int c = 0; c < MAX_CHANNELS; ++c)
                {
                    auto& ch = state->channels[static_cast<size_t>(c)];
                    auto& fade = crossfadeState[static_cast<size_t>(c)];
                    fade.length = fadeLen;
                    fade.remaining = 0;
                    fade.delay = state->hopSize;
                    for (int i = 0; i < fadeLen; ++i)
                    {
                        int idx = (ch.delayReadPos + state->hopSize + i) % (state->fftSize * 2);
                        fade.buffer[static_cast<size_t>(i)] = ch.latencyDelay[static_cast<size_t>(idx)];
                    }
                }
            }

            auto* readyIR = irReadyPtr.load(std::memory_order_acquire);
            auto* activeIR = irActivePtr.load(std::memory_order_acquire);
            irActivePtr.store(readyIR, std::memory_order_release);
            irReadyPtr.store(activeIR, std::memory_order_release);
            swapReady.store(SwapNone, std::memory_order_release);
        }

        int numChannels = juce::jmin(buffer.getNumChannels(), MAX_CHANNELS);
        for (int channel = 0; channel < numChannels; ++channel)
            processChannel(channel, buffer.getWritePointer(channel), buffer.getNumSamples());
    }

private:
    static constexpr int MAX_CHANNELS = 2;

    /** Per-channel convolution buffers and positions */
    struct ChannelState
    {
        std::vector<float> fftBuffer;    // Scratch for FFT computation
        std::vector<float> inputAccum;   // Circular input accumulation
        std::vector<float> outputAccum;  // Overlap-add output
        std::vector<float> latencyDelay; // Delay line for latency compensation

        int inputWritePos = 0;
        int outputReadPos = 0;
        int delayWritePos = 0;
        int delayReadPos = 0;
        int samplesInInputBuffer = 0;

        void allocate(int filterLength, int fftSize)
        {
            fftBuffer.resize(static_cast<size_t>(fftSize * 2), 0.0f);
            inputAccum.resize(static_cast<size_t>(filterLength), 0.0f);
            outputAccum.resize(static_cast<size_t>(fftSize * 2), 0.0f);
            latencyDelay.resize(static_cast<size_t>(fftSize * 2), 0.0f);
            delayWritePos = filterLength / 2;
        }

        void reset(int filterLength)
        {
            std::fill(inputAccum.begin(), inputAccum.end(), 0.0f);
            std::fill(outputAccum.begin(), outputAccum.end(), 0.0f);
            std::fill(latencyDelay.begin(), latencyDelay.end(), 0.0f);
            inputWritePos = 0;
            outputReadPos = 0;
            delayWritePos = filterLength / 2;
            delayReadPos = 0;
            samplesInInputBuffer = 0;
        }
    };

    /** Holds shared FFT config + per-channel state for a specific FFT size */
    struct ProcessorState
    {
        int filterLength;
        int fftOrder;
        int fftSize;
        int hopSize;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::array<ChannelState, MAX_CHANNELS> channels;

        explicit ProcessorState(int irLength)
            : filterLength(irLength)
            , fftOrder(static_cast<int>(std::log2(irLength)) + 1)
            , fftSize(irLength * 2)
            , hopSize(irLength / 2)
            , fft(std::make_unique<juce::dsp::FFT>(fftOrder))
        {
            for (auto& ch : channels)
                ch.allocate(filterLength, fftSize);
        }

        void reset()
        {
            for (auto& ch : channels)
                ch.reset(filterLength);
        }
    };

    /** Initialize an IR buffer with a flat (unity gain) response
        This is needed so audio passes through until the proper IR is built
        filterLength is the IR length, convolutionFftSize = 2 * filterLength */
    void initializeFlatIR(std::vector<float>* buffer, int filterLength)
    {
        int convFftSize = filterLength * 2;  // FFT size for linear convolution
        if (!buffer || buffer->size() < static_cast<size_t>(convFftSize * 2))
            return;

        // Simple approach: create centered impulse in time domain, then FFT
        // For unity gain with linear phase:
        // - Impulse at position filterLength/2 gives linear phase (constant group delay)
        // - Amplitude chosen to compensate for IFFT normalization in processFFTBlock

        std::vector<float> timeDomainIR(static_cast<size_t>(convFftSize * 2), 0.0f);

        // Place unit impulse at center of filter region
        // The impulse position determines the group delay (filterLength/2 samples)
        timeDomainIR[static_cast<size_t>(filterLength / 2)] = 1.0f;

        // Transform to frequency domain at convolution FFT size
        int convFftOrder = static_cast<int>(std::log2(convFftSize));
        juce::dsp::FFT convFft(convFftOrder);
        convFft.performRealOnlyForwardTransform(timeDomainIR.data());

        // Copy to output buffer - no additional normalization needed
        // The processFFTBlock normalizes IFFT by 1/fftSize which cancels forward FFT scaling
        std::copy(timeDomainIR.begin(), timeDomainIR.begin() + convFftSize * 2, buffer->begin());
    }

    void startBackgroundThread()
    {
        backgroundThreadRunning.store(true);
        backgroundThread = std::thread([this]() { backgroundThreadFunc(); });
    }

    void stopBackgroundThread()
    {
        backgroundThreadRunning.store(false);
        backgroundCV.notify_one();
        if (backgroundThread.joinable())
            backgroundThread.join();
    }

    void backgroundThreadFunc()
    {
        while (backgroundThreadRunning.load())
        {
            std::unique_lock<std::mutex> lock(backgroundMutex);
            backgroundCV.wait(lock, [this]() {
                return !backgroundThreadRunning.load() || irNeedsUpdate.load();
            });

            if (!backgroundThreadRunning.load())
                break;

            if (irNeedsUpdate.exchange(false))
            {
                // Wait for previous swap to be consumed before writing new data
                while (swapReady.load(std::memory_order_acquire) != SwapNone)
                {
                    if (!backgroundThreadRunning.load())
                        return;
                    lock.unlock();
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    lock.lock();
                }

                // Copy parameters under lock
                std::array<bool, 8> localBandEnabled;
                std::array<float, 8> localBandFreq;
                std::array<float, 8> localBandGain;
                std::array<float, 8> localBandQ;
                std::array<int, 2> localBandSlope;
                float localMasterGain;
                int localFftSize;
                bool lengthChanged;

                {
                    std::lock_guard<std::mutex> paramLock(paramMutex);
                    localBandEnabled = pendingBandEnabled;
                    localBandFreq = pendingBandFreq;
                    localBandGain = pendingBandGain;
                    localBandQ = pendingBandQ;
                    localBandSlope = pendingBandSlope;
                    localMasterGain = pendingMasterGain;
                    localFftSize = pendingFftSize.load(std::memory_order_acquire);
                    lengthChanged = filterLengthChanged.exchange(false);
                }

                // Handle filter length change - prepare new ProcessorState
                // This is separate from IR swap to avoid losing buffer history
                bool needsStateSwap = false;
                if (lengthChanged)
                {
                    // Wait for previous swap to be consumed BEFORE reading activeState
                    // to prevent data race with audio thread's std::swap
                    while (swapReady.load(std::memory_order_acquire) != SwapNone)
                    {
                        if (!backgroundThreadRunning.load())
                            return;
                        std::this_thread::yield();
                    }
                    
                    int currentActiveFilterLength = currentFilterLength.load(std::memory_order_acquire);
                    if (localFftSize != currentActiveFilterLength)
                    {
                        // Prepare a new ProcessorState with the new filter length
                        readyState = std::make_unique<ProcessorState>(localFftSize);
                        needsStateSwap = true;
                    }
                }

                // Rebuild IR with current parameters
                rebuildImpulseResponseBackground(
                    localBandEnabled, localBandFreq, localBandGain,
                    localBandQ, localBandSlope, localMasterGain, localFftSize);

                // Signal swap ready — single atomic ensures state+IR are swapped together
                swapReady.store(needsStateSwap ? SwapStateAndIR : SwapIR,
                                std::memory_order_release);
            }
        }
    }

    /** Rebuild IR on background thread - writes to the ready IR buffer
        workingFilterLength is the IR length, actual FFT is 2x for linear convolution */
    void rebuildImpulseResponseBackground(
        const std::array<bool, 8>& localBandEnabled,
        const std::array<float, 8>& localBandFreq,
        const std::array<float, 8>& localBandGain,
        const std::array<float, 8>& localBandQ,
        const std::array<int, 2>& localBandSlope,
        float localMasterGain,
        int workingFilterLength)
    {
        double sampleRate = currentSampleRate.load(std::memory_order_acquire);
        if (sampleRate <= 0)
            return;

        // FFT size for linear convolution = 2 * filter length
        int convolutionFftSize = workingFilterLength * 2;
        int convFftOrder = static_cast<int>(std::log2(convolutionFftSize));

        // Create FFTs for this computation
        int designFftOrder = static_cast<int>(std::log2(workingFilterLength));
        juce::dsp::FFT designFft(designFftOrder);
        juce::dsp::FFT convFft(convFftOrder);

        // Build magnitude response in frequency domain at the filter length resolution
        int designNumBins = workingFilterLength / 2 + 1;
        std::vector<float> magnitudeResponse(static_cast<size_t>(designNumBins), 1.0f);

        float nyquist = static_cast<float>(sampleRate) / 2.0f;

        for (int bin = 0; bin < designNumBins; ++bin)
        {
            float freq = static_cast<float>(bin) * nyquist / (static_cast<float>(workingFilterLength) * 0.5f);
            if (freq < 1.0f) freq = 1.0f;  // Avoid log(0)

            float totalGainLinear = 1.0f;

            // Apply each band's contribution
            for (int band = 0; band < 8; ++band)
            {
                if (!localBandEnabled[static_cast<size_t>(band)])
                    continue;

                float bandGainLinear = calculateBandGain(band, freq,
                    localBandFreq, localBandGain, localBandQ, localBandSlope);
                totalGainLinear *= bandGainLinear;
            }

            // Apply master gain
            float masterGainLinear = std::pow(10.0f, localMasterGain / 20.0f);
            totalGainLinear *= masterGainLinear;

            magnitudeResponse[static_cast<size_t>(bin)] = totalGainLinear;
        }

        // Create zero-phase (linear phase) IR at filter length resolution
        std::vector<float> designBuffer(static_cast<size_t>(workingFilterLength * 2), 0.0f);

        for (int bin = 0; bin < designNumBins; ++bin)
        {
            float mag = magnitudeResponse[static_cast<size_t>(bin)];
            // Real part = magnitude (phase = 0)
            designBuffer[static_cast<size_t>(bin * 2)] = mag;
            designBuffer[static_cast<size_t>(bin * 2 + 1)] = 0.0f;
        }

        // Inverse FFT to get time domain IR at filter length
        designFft.performRealOnlyInverseTransform(designBuffer.data());

        // NOTE: JUCE's real-only FFT/IFFT pair is already unity-gain on macOS (vDSP).
        // DO NOT normalize here - the round-trip FFT→IFFT already preserves amplitude.

        // Circular shift to center the IR (for linear phase)
        // This moves the impulse from sample 0 to sample filterLength/2
        std::vector<float> convBuffer(static_cast<size_t>(convolutionFftSize * 2), 0.0f);
        int halfSize = workingFilterLength / 2;
        for (int i = 0; i < workingFilterLength; ++i)
        {
            int srcIdx = (i + halfSize) % workingFilterLength;
            convBuffer[static_cast<size_t>(i)] = designBuffer[static_cast<size_t>(srcIdx)];
        }

        // Apply Kaiser window to reduce spectral leakage
        // Beta = 8 provides good stopband attenuation (~80dB) with reasonable transition width
        // Calculate DC response (sum of IR) BEFORE windowing to preserve gain
        float dcBefore = 0.0f;
        for (int i = 0; i < workingFilterLength; ++i)
            dcBefore += convBuffer[static_cast<size_t>(i)];

        juce::dsp::WindowingFunction<float> kaiserWindow(
            static_cast<size_t>(workingFilterLength),
            juce::dsp::WindowingFunction<float>::kaiser,
            false,  // Don't normalize - we compensate manually to preserve DC gain
            8.0f);  // Beta parameter
        kaiserWindow.multiplyWithWindowingTable(convBuffer.data(),
            static_cast<size_t>(workingFilterLength));

        // Calculate DC response AFTER windowing and compensate
        float dcAfter = 0.0f;
        for (int i = 0; i < workingFilterLength; ++i)
            dcAfter += convBuffer[static_cast<size_t>(i)];

        // Compensate for window's gain change to preserve intended frequency response
        if (std::abs(dcAfter) > 1e-10f)
        {
            float dcCompensation = dcBefore / dcAfter;
            for (int i = 0; i < workingFilterLength; ++i)
                convBuffer[static_cast<size_t>(i)] *= dcCompensation;
        }

        // Zero-padding from workingFilterLength to convolutionFftSize is already done by initialization

        // Transform IR to frequency domain at convolution FFT size
        // No additional normalization needed - the IFFT in processFFTBlock normalizes
        convFft.performRealOnlyForwardTransform(convBuffer.data());

        // Copy to ready IR buffer
        // Note: Buffer is pre-allocated to max size, no resize needed
        auto* readyIR = irReadyPtr.load(std::memory_order_acquire);
        if (readyIR)
        {
            // Copy the computed IR to the ready buffer
            std::copy(convBuffer.begin(),
                      convBuffer.begin() + convolutionFftSize * 2,
                      readyIR->begin());
        }
        // Note: IR swap signaling is handled by backgroundThreadFunc() after this returns
    }

    float calculateBandGain(int band, float freq,
        const std::array<float, 8>& localBandFreq,
        const std::array<float, 8>& localBandGain,
        const std::array<float, 8>& localBandQ,
        const std::array<int, 2>& localBandSlope) const
    {
        float bandFreqHz = localBandFreq[static_cast<size_t>(band)];
        float bandGainDb = localBandGain[static_cast<size_t>(band)];
        float bandQVal = localBandQ[static_cast<size_t>(band)];

        if (band == 0)  // HPF
        {
            int slopeIndex = localBandSlope[0];
            float slope = getSlopeFromIndex(slopeIndex);
            return calculateHPFGain(freq, bandFreqHz, slope);
        }
        else if (band == 7)  // LPF
        {
            int slopeIndex = localBandSlope[1];
            float slope = getSlopeFromIndex(slopeIndex);
            return calculateLPFGain(freq, bandFreqHz, slope);
        }
        else if (band == 1)  // Low Shelf
        {
            return calculateShelfGain(freq, bandFreqHz, bandGainDb, bandQVal, true);
        }
        else if (band == 6)  // High Shelf
        {
            return calculateShelfGain(freq, bandFreqHz, bandGainDb, bandQVal, false);
        }
        else  // Parametric (bands 2-5)
        {
            return calculateParametricGain(freq, bandFreqHz, bandGainDb, bandQVal);
        }
    }

    float getSlopeFromIndex(int index) const
    {
        static const float slopes[] = { 6.0f, 12.0f, 18.0f, 24.0f, 36.0f, 48.0f, 72.0f, 96.0f };
        if (index >= 0 && index < static_cast<int>(std::size(slopes)))
            return slopes[index];
        return 12.0f;
    }

    // Helper function to compute exact biquad magnitude response at a given frequency
    // Uses H(z) = (b0 + b1*z^-1 + b2*z^-2) / (a0 + a1*z^-1 + a2*z^-2)
    // evaluated at z = e^(j*omega)
    float calculateBiquadMagnitude(float freq, double sampleRate,
        float b0, float b1, float b2, float a0, float a1, float a2) const
    {
        double omega = juce::MathConstants<double>::twoPi * freq / sampleRate;
        double cosW = std::cos(omega);
        double cos2W = std::cos(2.0 * omega);
        double sinW = std::sin(omega);
        double sin2W = std::sin(2.0 * omega);

        // Numerator: b0 + b1*z^-1 + b2*z^-2 at z = e^(j*omega)
        double numRe = b0 + b1 * cosW + b2 * cos2W;
        double numIm = -b1 * sinW - b2 * sin2W;

        // Denominator: a0 + a1*z^-1 + a2*z^-2 at z = e^(j*omega)
        double denRe = a0 + a1 * cosW + a2 * cos2W;
        double denIm = -a1 * sinW - a2 * sin2W;

        double numMag = std::sqrt(numRe * numRe + numIm * numIm);
        double denMag = std::sqrt(denRe * denRe + denIm * denIm);

        return static_cast<float>(numMag / (denMag + 1e-10));
    }

    float calculateHPFGain(float freq, float cutoff, float slope) const
    {
        if (freq >= cutoff) return 1.0f;

        float ratio = freq / cutoff;
        if (ratio >= 1.0f) return 1.0f;

        float octavesBelow = std::log2(1.0f / ratio);
        float attenuationDb = octavesBelow * slope;
        return std::pow(10.0f, -attenuationDb / 20.0f);
    }

    float calculateLPFGain(float freq, float cutoff, float slope) const
    {
        if (freq <= cutoff) return 1.0f;

        float ratio = freq / cutoff;
        if (ratio <= 1.0f) return 1.0f;

        float octavesAbove = std::log2(ratio);
        float attenuationDb = octavesAbove * slope;
        return std::pow(10.0f, -attenuationDb / 20.0f);
    }

    // Calculates exact shelf filter magnitude response using RBJ cookbook biquad coefficients
    float calculateShelfGain(float freq, float shelfFreq, float gainDb, float q, bool isLowShelf) const
    {
        if (std::abs(gainDb) < 0.01f) return 1.0f;

        double sr = currentSampleRate.load(std::memory_order_acquire);
        if (sr <= 0) sr = 44100.0;

        // Calculate shelf filter coefficients (RBJ Audio EQ Cookbook)
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = juce::MathConstants<float>::twoPi * shelfFreq / static_cast<float>(sr);
        float sinW0 = std::sin(w0);
        float cosW0 = std::cos(w0);
        float qVal = juce::jmax(0.1f, q);
        float alpha = sinW0 / (2.0f * qVal);

        float b0, b1, b2, a0, a1, a2;
        if (isLowShelf)
        {
            float sqrtA = std::sqrt(A);
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cosW0 + 2.0f * sqrtA * alpha);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW0);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cosW0 - 2.0f * sqrtA * alpha);
            a0 = (A + 1.0f) + (A - 1.0f) * cosW0 + 2.0f * sqrtA * alpha;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW0);
            a2 = (A + 1.0f) + (A - 1.0f) * cosW0 - 2.0f * sqrtA * alpha;
        }
        else  // High shelf
        {
            float sqrtA = std::sqrt(A);
            b0 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 + 2.0f * sqrtA * alpha);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW0);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 - 2.0f * sqrtA * alpha);
            a0 = (A + 1.0f) - (A - 1.0f) * cosW0 + 2.0f * sqrtA * alpha;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosW0);
            a2 = (A + 1.0f) - (A - 1.0f) * cosW0 - 2.0f * sqrtA * alpha;
        }

        // Normalize coefficients
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

        return calculateBiquadMagnitude(freq, sr, b0, b1, b2, 1.0f, a1, a2);
    }

    // Calculates exact peaking filter magnitude response using RBJ cookbook biquad coefficients
    float calculateParametricGain(float freq, float centerFreq, float gainDb, float q) const
    {
        if (std::abs(gainDb) < 0.01f) return 1.0f;

        double sr = currentSampleRate.load(std::memory_order_acquire);
        if (sr <= 0) sr = 44100.0;

        // Calculate peaking filter coefficients (RBJ Audio EQ Cookbook)
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = juce::MathConstants<float>::twoPi * centerFreq / static_cast<float>(sr);
        float sinW0 = std::sin(w0);
        float cosW0 = std::cos(w0);
        float qVal = juce::jmax(0.1f, q);
        float alpha = sinW0 / (2.0f * qVal);

        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cosW0;
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cosW0;
        float a2 = 1.0f - alpha / A;

        // Normalize coefficients
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

        return calculateBiquadMagnitude(freq, sr, b0, b1, b2, 1.0f, a1, a2);
    }

    void processFFTBlock(ProcessorState* state, ChannelState* ch, std::vector<float>* irBuffer)
    {
        if (!state || !state->fft || !ch || !irBuffer)
            return;

        int filterLength = state->filterLength;
        int fftSize = state->fftSize;
        int hopSize = state->hopSize;

        // Gather the last filterLength samples from input accumulator (circular buffer)
        for (int i = 0; i < filterLength; ++i)
        {
            int readIdx = (ch->inputWritePos - filterLength + i + filterLength) % filterLength;
            ch->fftBuffer[static_cast<size_t>(i)] = ch->inputAccum[static_cast<size_t>(readIdx)];
        }

        // Zero-pad from filterLength to fftSize for linear convolution
        std::fill(ch->fftBuffer.begin() + filterLength, ch->fftBuffer.begin() + fftSize, 0.0f);
        std::fill(ch->fftBuffer.begin() + fftSize, ch->fftBuffer.end(), 0.0f);

        state->fft->performRealOnlyForwardTransform(ch->fftBuffer.data());

        // Complex multiplication in frequency domain
        int numBins = fftSize / 2 + 1;
        for (int bin = 0; bin < numBins; ++bin)
        {
            size_t idx = static_cast<size_t>(bin * 2);
            float inRe = ch->fftBuffer[idx];
            float inIm = ch->fftBuffer[idx + 1];
            float irRe = (*irBuffer)[idx];
            float irIm = (*irBuffer)[idx + 1];

            ch->fftBuffer[idx] = inRe * irRe - inIm * irIm;
            ch->fftBuffer[idx + 1] = inRe * irIm + inIm * irRe;
        }

        state->fft->performRealOnlyInverseTransform(ch->fftBuffer.data());

        // Overlap-add: accumulate the full linear convolution result (fftSize samples)
        for (int i = 0; i < fftSize; ++i)
        {
            int writeIdx = (ch->outputReadPos + i) % (fftSize * 2);
            ch->outputAccum[static_cast<size_t>(writeIdx)] += ch->fftBuffer[static_cast<size_t>(i)];
        }

        // Transfer hopSize samples from output accumulator to latency delay
        // 50% overlap → divide by 2 to compensate for overlap-add doubling
        for (int i = 0; i < hopSize; ++i)
        {
            int readIdx = (ch->outputReadPos + i) % (fftSize * 2);
            ch->latencyDelay[static_cast<size_t>(ch->delayWritePos)] = ch->outputAccum[static_cast<size_t>(readIdx)] * 0.5f;
            ch->outputAccum[static_cast<size_t>(readIdx)] = 0.0f;
            ch->delayWritePos = (ch->delayWritePos + 1) % (fftSize * 2);
        }

        ch->outputReadPos = (ch->outputReadPos + hopSize) % (fftSize * 2);
    }

    // Double-buffered ProcessorState for filter length changes
    std::unique_ptr<ProcessorState> activeState;
    std::unique_ptr<ProcessorState> readyState;

    // Double-buffered IR via atomic pointers (no audio-thread allocations)
    // Buffers are pre-allocated to maximum size (Long * 2 = 32768) to avoid resize()
    std::unique_ptr<std::vector<float>> irBufferA;
    std::unique_ptr<std::vector<float>> irBufferB;
    std::atomic<std::vector<float>*> irActivePtr{nullptr};
    std::atomic<std::vector<float>*> irReadyPtr{nullptr};

    int maxBlockSize = 512;

    // Per-channel crossfade state for smooth IR transitions
    struct CrossfadeState
    {
        std::vector<float> buffer;  // Pre-swap output snapshot
        int remaining = 0;          // Samples remaining in crossfade
        int length = 0;             // Total crossfade length (= hopSize)
        int delay = 0;              // Samples to wait before starting crossfade
    };
    std::array<CrossfadeState, MAX_CHANNELS> crossfadeState;

    // Parameters (protected by mutex for background thread access)
    std::atomic<double> currentSampleRate{44100.0};
    std::mutex paramMutex;
    std::array<bool, 8> pendingBandEnabled{};
    std::array<float, 8> pendingBandFreq{};
    std::array<float, 8> pendingBandGain{};
    std::array<float, 8> pendingBandQ{};
    std::array<int, 2> pendingBandSlope{};
    float pendingMasterGain = 0.0f;

    // Thread synchronization
    // Single atomic flag for swap readiness to prevent state/IR size mismatch.
    // When filter length changes, both state AND IR must be swapped atomically.
    enum SwapMode { SwapNone = 0, SwapIR = 1, SwapStateAndIR = 2 };
    std::atomic<int> swapReady{SwapNone};
    std::atomic<bool> irNeedsUpdate{true};
    std::atomic<int> pendingFftSize{8192};
    std::atomic<bool> filterLengthChanged{false};
    std::atomic<int> currentFilterLength{8192};  // Thread-safe filter length for UI queries

    // Background thread for IR computation
    std::thread backgroundThread;
    std::mutex backgroundMutex;
    std::condition_variable backgroundCV;
    std::atomic<bool> backgroundThreadRunning{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinearPhaseEQProcessor)
};

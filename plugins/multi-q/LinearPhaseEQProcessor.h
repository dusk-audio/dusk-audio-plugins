#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

//==============================================================================
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
        // Initialize both IR buffers to MAXIMUM size to avoid any resize() during operation
        // This prevents race conditions where resize() could be called while audio thread reads
        // Max filter length = Long (16384), convolution FFT size = 2 * Long = 32768
        // Buffer needs 2 * convolution FFT size for complex frequency domain data
        static constexpr size_t maxConvFftSize = static_cast<size_t>(Long) * 2;  // 32768
        static constexpr size_t maxIrBufferSize = maxConvFftSize * 2;            // 65536
        irBufferA = std::make_unique<std::vector<float>>(maxIrBufferSize, 0.0f);
        irBufferB = std::make_unique<std::vector<float>>(maxIrBufferSize, 0.0f);

        // CRITICAL: Initialize BOTH IR buffers with flat response (unity gain, zero phase)
        // This ensures audio passes through regardless of which buffer is active
        initializeFlatIR(irBufferA.get(), Medium);  // Default filter length
        initializeFlatIR(irBufferB.get(), Medium);  // Both buffers start with flat IR

        irActivePtr.store(irBufferA.get(), std::memory_order_release);
        irReadyPtr.store(irBufferB.get(), std::memory_order_release);

        // Initialize active processor state with default filter length
        activeState = std::make_unique<ProcessorState>(Medium);
        // Initialize ready state (will be swapped in when filter length changes)
        readyState = std::make_unique<ProcessorState>(Medium);

        startBackgroundThread();
    }

    ~LinearPhaseEQProcessor()
    {
        stopBackgroundThread();
    }

    //==========================================================================
    void prepare(double sampleRate, int newMaxBlockSize)
    {
        currentSampleRate.store(sampleRate, std::memory_order_release);
        maxBlockSize = newMaxBlockSize;
        reset();
        // Mark for IR rebuild on background thread
        irNeedsUpdate.store(true);
        backgroundCV.notify_one();
    }

    void reset()
    {
        auto* state = activeState.get();
        if (!state) return;

        std::fill(state->inputAccum.begin(), state->inputAccum.end(), 0.0f);
        std::fill(state->outputAccum.begin(), state->outputAccum.end(), 0.0f);
        std::fill(state->latencyDelay.begin(), state->latencyDelay.end(), 0.0f);
        state->inputWritePos = 0;
        state->outputReadPos = 0;
        // Start write position ahead of read position by the latency amount
        state->delayWritePos = state->filterLength / 2;
        state->delayReadPos = 0;
        state->samplesInInputBuffer = 0;
    }

    //==========================================================================
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
        auto* state = activeState.get();
        return state ? state->filterLength : 8192;
    }

    //==========================================================================
    /**
        Returns the latency in samples.
        Linear phase EQ has inherent latency of filterLength/2 samples.
    */
    int getLatencyInSamples() const
    {
        auto* state = activeState.get();
        return state ? state->filterLength / 2 : 4096;
    }

    //==========================================================================
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
        float newMasterGain,
        const std::array<int, 8>& newBandShape = {})
    {
        // Store parameters atomically (using mutex for the arrays)
        {
            std::lock_guard<std::mutex> lock(paramMutex);
            pendingBandEnabled = newBandEnabled;
            pendingBandFreq = newBandFreq;
            pendingBandGain = newBandGain;
            pendingBandQ = newBandQ;
            pendingBandSlope = newBandSlope;
            pendingBandShape = newBandShape;
            pendingMasterGain = newMasterGain;
        }
        irNeedsUpdate.store(true);
        backgroundCV.notify_one();
    }

    //==========================================================================
    /** Process a single channel of audio using overlap-add FFT convolution */
    void processChannel(float* channelData, int numSamples)
    {
        // Swap IR buffer if ready (happens on every EQ parameter change)
        // This is separate from state swap to preserve circular buffer history
        if (irSwapReady.load(std::memory_order_acquire))
        {
            auto* readyIR = irReadyPtr.load(std::memory_order_acquire);
            auto* activeIR = irActivePtr.load(std::memory_order_acquire);
            irActivePtr.store(readyIR, std::memory_order_release);
            irReadyPtr.store(activeIR, std::memory_order_release);
            irSwapReady.store(false, std::memory_order_release);
        }

        // Swap ProcessorState only when filter LENGTH changes
        // This loses buffer history but is unavoidable when FFT size changes
        if (stateSwapReady.load(std::memory_order_acquire))
        {
            std::swap(activeState, readyState);
            stateSwapReady.store(false, std::memory_order_release);
        }

        auto* state = activeState.get();
        if (!state || !state->fft)
            return;

        // Get the active IR buffer - needs fftSize * 2 for the frequency domain data
        auto* irBuffer = irActivePtr.load(std::memory_order_acquire);
        if (!irBuffer || irBuffer->size() < static_cast<size_t>(state->fftSize * 2))
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // Store input sample in accumulation buffer (circular, filterLength size)
            state->inputAccum[static_cast<size_t>(state->inputWritePos)] = channelData[i];
            state->inputWritePos = (state->inputWritePos + 1) % state->filterLength;
            state->samplesInInputBuffer++;

            // When we have a hop's worth of new samples, process FFT
            if (state->samplesInInputBuffer >= state->hopSize)
            {
                processFFTBlock(state, irBuffer);
                state->samplesInInputBuffer = 0;
            }

            // Read output from latency delay buffer
            channelData[i] = state->latencyDelay[static_cast<size_t>(state->delayReadPos)];
            state->delayReadPos = (state->delayReadPos + 1) % (state->fftSize * 2);
        }
    }

    /** Process stereo audio */
    void processStereo(juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            processChannel(buffer.getWritePointer(channel), buffer.getNumSamples());
        }
    }

private:
    //==========================================================================
    /** Holds all state needed for processing at a specific FFT size */
    struct ProcessorState
    {
        // Note: Declaration order must match initialization order
        int filterLength;       // The IR/filter length (e.g., 4096, 8192, 16384)
        int fftOrder;           // Order for 2x filter length FFT (for linear convolution)
        int fftSize;            // Actual FFT size = 2 * filterLength (for zero-padding)
        int hopSize;            // Hop size = filterLength / 2 (50% overlap)
        std::unique_ptr<juce::dsp::FFT> fft;

        std::vector<float> fftBuffer;
        std::vector<float> inputAccum;
        std::vector<float> outputAccum;
        std::vector<float> latencyDelay;

        int inputWritePos = 0;
        int outputReadPos = 0;
        int delayWritePos = 0;
        int delayReadPos = 0;
        int samplesInInputBuffer = 0;

        explicit ProcessorState(int irLength)
            : filterLength(irLength)
            , fftOrder(static_cast<int>(std::log2(irLength)) + 1)  // +1 for 2x size (linear convolution)
            , fftSize(irLength * 2)                                  // FFT size = 2 * filter length
            , hopSize(irLength / 2)                                  // 50% overlap of filter length
            , fft(std::make_unique<juce::dsp::FFT>(fftOrder))
        {
            // fftBuffer needs to hold 2*fftSize for JUCE's real-only FFT format
            fftBuffer.resize(static_cast<size_t>(fftSize * 2), 0.0f);
            // inputAccum holds filterLength samples (one IR length)
            inputAccum.resize(static_cast<size_t>(filterLength), 0.0f);
            // outputAccum and latencyDelay sized for overlap-add output
            outputAccum.resize(static_cast<size_t>(fftSize * 2), 0.0f);
            latencyDelay.resize(static_cast<size_t>(fftSize * 2), 0.0f);
            delayWritePos = filterLength / 2;  // Start ahead by latency amount
        }

        void reset()
        {
            std::fill(inputAccum.begin(), inputAccum.end(), 0.0f);
            std::fill(outputAccum.begin(), outputAccum.end(), 0.0f);
            std::fill(latencyDelay.begin(), latencyDelay.end(), 0.0f);
            inputWritePos = 0;
            outputReadPos = 0;
            delayWritePos = filterLength / 2;  // Latency = half the filter length
            delayReadPos = 0;
            samplesInInputBuffer = 0;
        }
    };

    //==========================================================================
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

    //==========================================================================
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
                // Wait for IR swap to be consumed before writing new IR
                while (irSwapReady.load(std::memory_order_acquire))
                {
                    if (!backgroundThreadRunning.load())
                        return;
                    std::this_thread::yield();
                }

                // Copy parameters under lock
                std::array<bool, 8> localBandEnabled;
                std::array<float, 8> localBandFreq;
                std::array<float, 8> localBandGain;
                std::array<float, 8> localBandQ;
                std::array<int, 2> localBandSlope;
                std::array<int, 8> localBandShape;
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
                    localBandShape = pendingBandShape;
                    localMasterGain = pendingMasterGain;
                    localFftSize = pendingFftSize.load(std::memory_order_acquire);
                    lengthChanged = filterLengthChanged.exchange(false);
                }

                // Handle filter length change - prepare new ProcessorState
                // This is separate from IR swap to avoid losing buffer history
                bool needsStateSwap = false;
                if (lengthChanged)
                {
                    int currentActiveFilterLength = activeState ? activeState->filterLength : Medium;
                    if (localFftSize != currentActiveFilterLength)
                    {
                        // Wait for previous state swap to be consumed
                        while (stateSwapReady.load(std::memory_order_acquire))
                        {
                            if (!backgroundThreadRunning.load())
                                return;
                            std::this_thread::yield();
                        }

                        // Prepare a new ProcessorState with the new filter length
                        readyState = std::make_unique<ProcessorState>(localFftSize);
                        needsStateSwap = true;
                    }
                }

                // Rebuild IR with current parameters
                rebuildImpulseResponseBackground(
                    localBandEnabled, localBandFreq, localBandGain,
                    localBandQ, localBandSlope, localBandShape, localMasterGain, localFftSize);

                // Signal IR is ready to swap
                irSwapReady.store(true, std::memory_order_release);

                // Signal state is ready only if filter length changed
                if (needsStateSwap)
                    stateSwapReady.store(true, std::memory_order_release);
            }
        }
    }

    //==========================================================================
    /** Rebuild IR on background thread - writes to the ready IR buffer
        workingFilterLength is the IR length, actual FFT is 2x for linear convolution */
    void rebuildImpulseResponseBackground(
        const std::array<bool, 8>& localBandEnabled,
        const std::array<float, 8>& localBandFreq,
        const std::array<float, 8>& localBandGain,
        const std::array<float, 8>& localBandQ,
        const std::array<int, 2>& localBandSlope,
        const std::array<int, 8>& localBandShape,
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
                    localBandFreq, localBandGain, localBandQ, localBandSlope, localBandShape);
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

    //==========================================================================
    float calculateBandGain(int band, float freq,
        const std::array<float, 8>& localBandFreq,
        const std::array<float, 8>& localBandGain,
        const std::array<float, 8>& localBandQ,
        const std::array<int, 2>& localBandSlope,
        const std::array<int, 8>& localBandShape) const
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
        else if (band == 1)  // Band 2: shape-aware
        {
            int shape = localBandShape[1];
            if (shape == 1)  // Peaking
                return calculateParametricGain(freq, bandFreqHz, bandGainDb, bandQVal);
            else if (shape == 2)  // High-Pass (12 dB/oct)
                return calculateHPFGain(freq, bandFreqHz, 12.0f);
            else  // Low Shelf (default)
                return calculateShelfGain(freq, bandFreqHz, bandGainDb, bandQVal, true);
        }
        else if (band == 6)  // Band 7: shape-aware
        {
            int shape = localBandShape[6];
            if (shape == 1)  // Peaking
                return calculateParametricGain(freq, bandFreqHz, bandGainDb, bandQVal);
            else if (shape == 2)  // Low-Pass (12 dB/oct)
                return calculateLPFGain(freq, bandFreqHz, 12.0f);
            else  // High Shelf (default)
                return calculateShelfGain(freq, bandFreqHz, bandGainDb, bandQVal, false);
        }
        else  // Parametric (bands 2-5) - shape-aware
        {
            int shape = localBandShape[static_cast<size_t>(band)];
            if (shape == 3)  // Tilt Shelf
                return calculateTiltShelfGain(freq, bandFreqHz, bandGainDb);
            else
                return calculateParametricGain(freq, bandFreqHz, bandGainDb, bandQVal);
        }
    }

    float getSlopeFromIndex(int index) const
    {
        static const float slopes[] = { 6.0f, 12.0f, 18.0f, 24.0f, 36.0f, 48.0f, 72.0f, 96.0f };
        if (index >= 0 && index < 8)
            return slopes[index];
        return 12.0f;
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

    float calculateShelfGain(float freq, float shelfFreq, float gainDb, float /*q*/, bool isLowShelf) const
    {
        if (std::abs(gainDb) < 0.01f) return 1.0f;

        float gainLinear = std::pow(10.0f, gainDb / 20.0f);
        float ratio = freq / shelfFreq;

        if (isLowShelf)
        {
            if (ratio < 0.5f) return gainLinear;
            if (ratio > 2.0f) return 1.0f;

            float t = (std::log2(ratio) + 1.0f) / 2.0f;
            t = juce::jlimit(0.0f, 1.0f, t);
            return gainLinear + (1.0f - gainLinear) * t;
        }
        else
        {
            if (ratio > 2.0f) return gainLinear;
            if (ratio < 0.5f) return 1.0f;

            float t = (std::log2(ratio) + 1.0f) / 2.0f;
            t = juce::jlimit(0.0f, 1.0f, t);
            return 1.0f + (gainLinear - 1.0f) * t;
        }
    }

    float calculateParametricGain(float freq, float centerFreq, float gainDb, float q) const
    {
        if (std::abs(gainDb) < 0.01f) return 1.0f;

        float ratio = freq / centerFreq;
        float logRatio = std::log2(ratio);

        float bandwidth = 1.0f / juce::jmax(0.1f, q);
        float x = logRatio / bandwidth;

        float response = std::exp(-0.5f * x * x);

        float gainLinear = std::pow(10.0f, gainDb / 20.0f);
        return 1.0f + (gainLinear - 1.0f) * response;
    }

    float calculateTiltShelfGain(float freq, float centerFreq, float gainDb) const
    {
        if (std::abs(gainDb) < 0.01f) return 1.0f;

        float tiltRatio = freq / centerFreq;
        float tiltTransition = 2.0f / juce::MathConstants<float>::pi
            * std::atan(std::log2(tiltRatio) * 2.0f);
        float tiltGainDB = gainDb * tiltTransition;
        return std::pow(10.0f, tiltGainDB / 20.0f);
    }

    //==========================================================================
    void processFFTBlock(ProcessorState* state, std::vector<float>* irBuffer)
    {
        if (!state || !state->fft || !irBuffer)
            return;

        int filterLength = state->filterLength;
        int fftSize = state->fftSize;  // = 2 * filterLength
        int hopSize = state->hopSize;

        // Gather the last filterLength samples from input accumulator (circular buffer)
        for (int i = 0; i < filterLength; ++i)
        {
            int readIdx = (state->inputWritePos - filterLength + i + filterLength) % filterLength;
            state->fftBuffer[static_cast<size_t>(i)] = state->inputAccum[static_cast<size_t>(readIdx)];
        }

        // Zero-pad from filterLength to fftSize for linear convolution
        std::fill(state->fftBuffer.begin() + filterLength, state->fftBuffer.begin() + fftSize, 0.0f);
        // Clear the second half (used for complex output)
        std::fill(state->fftBuffer.begin() + fftSize, state->fftBuffer.end(), 0.0f);

        // Forward FFT of input (fftSize points)
        state->fft->performRealOnlyForwardTransform(state->fftBuffer.data());

        // Complex multiplication in frequency domain
        int numBins = fftSize / 2 + 1;
        for (int bin = 0; bin < numBins; ++bin)
        {
            size_t idx = static_cast<size_t>(bin * 2);
            float inRe = state->fftBuffer[idx];
            float inIm = state->fftBuffer[idx + 1];
            float irRe = (*irBuffer)[idx];
            float irIm = (*irBuffer)[idx + 1];

            // Complex multiplication: (a+bi)(c+di) = (ac-bd) + (ad+bc)i
            state->fftBuffer[idx] = inRe * irRe - inIm * irIm;
            state->fftBuffer[idx + 1] = inRe * irIm + inIm * irRe;
        }

        // Inverse FFT
        state->fft->performRealOnlyInverseTransform(state->fftBuffer.data());

        // NOTE: JUCE's real-only FFT/IFFT pair is already unity-gain on macOS (vDSP).
        // DO NOT normalize here - the round-trip FFT→IFFT already preserves amplitude.

        // Overlap-add: accumulate the full linear convolution result (fftSize samples)
        for (int i = 0; i < fftSize; ++i)
        {
            int writeIdx = (state->outputReadPos + i) % (fftSize * 2);
            state->outputAccum[static_cast<size_t>(writeIdx)] += state->fftBuffer[static_cast<size_t>(i)];
        }

        // Transfer hopSize samples from output accumulator to latency delay
        // With 50% overlap and no windowing, each sample receives contributions from
        // 2 FFT blocks, so we divide by 2 to compensate for the overlap-add doubling.
        for (int i = 0; i < hopSize; ++i)
        {
            int readIdx = (state->outputReadPos + i) % (fftSize * 2);
            state->latencyDelay[static_cast<size_t>(state->delayWritePos)] = state->outputAccum[static_cast<size_t>(readIdx)] * 0.5f;
            state->outputAccum[static_cast<size_t>(readIdx)] = 0.0f;  // Clear for next overlap
            state->delayWritePos = (state->delayWritePos + 1) % (fftSize * 2);
        }

        // Advance output read position by hop size
        state->outputReadPos = (state->outputReadPos + hopSize) % (fftSize * 2);
    }

    //==========================================================================
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

    // Parameters (protected by mutex for background thread access)
    std::atomic<double> currentSampleRate{44100.0};
    std::mutex paramMutex;
    std::array<bool, 8> pendingBandEnabled{};
    std::array<float, 8> pendingBandFreq{};
    std::array<float, 8> pendingBandGain{};
    std::array<float, 8> pendingBandQ{};
    std::array<int, 8> pendingBandShape{};
    std::array<int, 2> pendingBandSlope{};
    float pendingMasterGain = 0.0f;

    // Thread synchronization
    // Separate flags for IR buffer swap vs ProcessorState swap:
    // - irSwapReady: IR buffer is ready to swap (happens on every parameter change)
    // - stateSwapReady: ProcessorState is ready to swap (only when filter LENGTH changes)
    // This separation prevents losing circular buffer history during normal IR updates.
    std::atomic<bool> irSwapReady{false};
    std::atomic<bool> stateSwapReady{false};
    std::atomic<bool> irNeedsUpdate{true};
    std::atomic<int> pendingFftSize{8192};
    std::atomic<bool> filterLengthChanged{false};

    // Background thread for IR computation
    std::thread backgroundThread;
    std::mutex backgroundMutex;
    std::condition_variable backgroundCV;
    std::atomic<bool> backgroundThreadRunning{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinearPhaseEQProcessor)
};

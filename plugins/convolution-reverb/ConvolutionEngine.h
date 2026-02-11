/*
  ==============================================================================

    Convolution Reverb - Convolution Engine
    Wrapper around juce::dsp::Convolution with envelope and reverse support
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <mutex>
#include "EnvelopeProcessor.h"
#include "AifcStreamWrapper.h"

class ConvolutionEngine
{
public:
    // Quality levels (sample rate divisors)
    enum class Quality
    {
        LoFi = 0,    // 1/4 sample rate
        Low = 1,     // 1/2 sample rate
        Medium = 2,  // Full sample rate
        High = 3     // Full sample rate (same as Medium currently)
    };

    // Stereo mode for IR processing
    enum class StereoMode
    {
        TrueStereo = 0,      // Use stereo IR as-is (L/R channels independent)
        MonoToStereo = 1     // Sum IR to mono, then process both channels identically
    };

    ConvolutionEngine() = default;
    ~ConvolutionEngine() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSpec = spec;
        convolution.prepare(spec);
        convolution.reset();

        // Prepare filter envelope filter
        filterEnvFilter.prepare(spec);
        filterEnvFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    }

    void reset()
    {
        convolution.reset();
        filterEnvFilter.reset();
        filterEnvPosition = 0;
    }

    void loadImpulseResponse(const juce::File& file, double targetSampleRate)
    {
        if (!file.existsAsFile())
            return;

        // Load the IR file
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        // Use createReaderForAudioFile which handles AIFC files with non-standard
        // compression types like 'in24' (used by Space Designer .SDIR files)
        std::unique_ptr<juce::AudioFormatReader> reader = createReaderForAudioFile(formatManager, file);

        if (reader != nullptr)
        {
            originalIR.setSize(static_cast<int>(reader->numChannels),
                               static_cast<int>(reader->lengthInSamples));
            reader->read(&originalIR, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
            originalSampleRate = reader->sampleRate;

            // Store for rebuilding
            this->targetSampleRate = targetSampleRate;

            // Build processed IR and load (called from message thread during load)
            rebuildProcessedIR();
        }
    }

    // Call this from processBlock - only processes audio, never allocates
    // inputBuffer is optional - if provided, used for transient detection
    void processBlock(juce::AudioBuffer<float>& buffer, const EnvelopeProcessor& envelope,
                      const juce::AudioBuffer<float>* inputBuffer = nullptr)
    {
        // Check if envelope parameters changed - set flag for deferred rebuild
        // This is real-time safe: only atomic operations, no allocations
        if (envelopeChanged(envelope))
        {
            pendingAttack.store(envelope.getAttack(), std::memory_order_relaxed);
            pendingDecay.store(envelope.getDecay(), std::memory_order_relaxed);
            pendingLength.store(envelope.getLength(), std::memory_order_relaxed);
            needsRebuild.store(true, std::memory_order_release);
        }

        // Transient detection for filter envelope reset
        if (filterEnvEnabled && inputBuffer != nullptr)
        {
            detectTransientAndResetFilter(*inputBuffer);
        }

        // Process convolution (real-time safe)
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        convolution.process(context);

        // Apply filter envelope if enabled
        if (filterEnvEnabled)
        {
            processFilterEnvelope(buffer);
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* inputBuffer = nullptr)
    {
        // Transient detection for filter envelope reset
        if (filterEnvEnabled && inputBuffer != nullptr)
        {
            detectTransientAndResetFilter(*inputBuffer);
        }

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        convolution.process(context);

        // Apply filter envelope if enabled
        if (filterEnvEnabled)
        {
            processFilterEnvelope(buffer);
        }
    }

    // Detect transients in input signal and reset filter envelope when triggered
    void detectTransientAndResetFilter(const juce::AudioBuffer<float>& inputBuffer)
    {
        // Calculate peak level of current input block
        float currentLevel = 0.0f;
        for (int channel = 0; channel < inputBuffer.getNumChannels(); ++channel)
        {
            currentLevel = std::max(currentLevel, inputBuffer.getMagnitude(channel, 0, inputBuffer.getNumSamples()));
        }

        // Check for silence (below threshold)
        if (currentLevel < transientThreshold * 0.1f)
        {
            silenceSampleCount += inputBuffer.getNumSamples();
        }
        else
        {
            // Check for transient: signal rises significantly after period of silence
            bool wasInSilence = (silenceSampleCount > silenceThresholdSamples);
            bool isRisingEdge = (currentLevel > previousInputLevel * 2.0f) && (currentLevel > transientThreshold);

            if (wasInSilence && isRisingEdge)
            {
                // Transient detected - reset filter envelope
                resetFilterEnvelope();
            }

            silenceSampleCount = 0;
        }

        previousInputLevel = currentLevel;
    }

    // Process filter envelope - sweeps cutoff frequency over the reverb tail
    void processFilterEnvelope(juce::AudioBuffer<float>& buffer)
    {
        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            // Calculate envelope position (0-1)
            float envPosition = (filterEnvTotalSamples > 0)
                ? static_cast<float>(filterEnvPosition) / static_cast<float>(filterEnvTotalSamples)
                : 1.0f;

            envPosition = juce::jlimit(0.0f, 1.0f, envPosition);

            // Calculate filter cutoff based on envelope
            // Attack phase: stay at init freq, then sweep to end freq
            float cutoff;
            if (envPosition < filterEnvAttack)
            {
                // During attack, stay at initial frequency
                cutoff = filterEnvInitFreq;
            }
            else
            {
                // After attack, sweep from init to end frequency
                float sweepPosition = (filterEnvAttack < 1.0f)
                    ? (envPosition - filterEnvAttack) / (1.0f - filterEnvAttack)
                    : 1.0f;
                sweepPosition = juce::jlimit(0.0f, 1.0f, sweepPosition);

                // Logarithmic interpolation for more natural frequency sweep
                float logInit = std::log(filterEnvInitFreq);
                float logEnd = std::log(filterEnvEndFreq);
                cutoff = std::exp(logInit + sweepPosition * (logEnd - logInit));
            }

            cutoff = juce::jlimit(200.0f, 20000.0f, cutoff);
            filterEnvFilter.setCutoffFrequency(cutoff);

            // Process each channel sample-by-sample using processSample
            for (int channel = 0; channel < numChannels; ++channel)
            {
                float* data = buffer.getWritePointer(channel);
                data[i] = filterEnvFilter.processSample(channel, data[i]);
            }

            // Advance envelope position
            ++filterEnvPosition;

            // Don't let position wrap around (stays at end)
            if (filterEnvPosition > filterEnvTotalSamples * 2)
                filterEnvPosition = filterEnvTotalSamples * 2;
        }
    }

    // Call this from a non-audio thread (e.g., timer callback) to apply pending changes
    void applyPendingChanges()
    {
        if (needsRebuild.exchange(false, std::memory_order_acquire))
        {
            cachedAttack = pendingAttack.load(std::memory_order_relaxed);
            cachedDecay = pendingDecay.load(std::memory_order_relaxed);
            cachedLength = pendingLength.load(std::memory_order_relaxed);
            rebuildProcessedIR();
        }
    }

    // Check if there are pending changes (for UI feedback)
    bool hasPendingChanges() const
    {
        return needsRebuild.load(std::memory_order_relaxed);
    }

    void setReverse(bool shouldReverse)
    {
        if (reversed != shouldReverse)
        {
            reversed = shouldReverse;
            rebuildProcessedIR();
        }
    }

    bool isReversed() const { return reversed; }

    void setZeroLatency(bool zeroLatency)
    {
        if (useZeroLatency != zeroLatency)
        {
            useZeroLatency = zeroLatency;
            rebuildProcessedIR();
        }
    }

    bool isZeroLatency() const { return useZeroLatency; }

    // IR Offset (0-1, percentage of IR to skip from the start)
    void setIROffset(float offset)
    {
        float newOffset = juce::jlimit(0.0f, 0.5f, offset);
        if (std::abs(irOffset - newOffset) > 0.001f)
        {
            irOffset = newOffset;
            rebuildProcessedIR();
        }
    }

    float getIROffset() const { return irOffset; }

    // Quality (sample rate control)
    void setQuality(Quality q)
    {
        if (quality != q)
        {
            quality = q;
            rebuildProcessedIR();
        }
    }

    Quality getQuality() const { return quality; }

    // Stereo mode
    void setStereoMode(StereoMode mode)
    {
        if (stereoMode != mode)
        {
            stereoMode = mode;
            rebuildProcessedIR();
        }
    }

    StereoMode getStereoMode() const { return stereoMode; }

    // Volume compensation
    void setVolumeCompensation(bool enabled)
    {
        if (volumeCompensation != enabled)
        {
            volumeCompensation = enabled;
            rebuildProcessedIR();
        }
    }

    bool isVolumeCompensationEnabled() const { return volumeCompensation; }

    // Filter envelope parameters
    void setFilterEnvelopeEnabled(bool enabled)
    {
        filterEnvEnabled = enabled;
        if (!enabled)
        {
            filterEnvFilter.setCutoffFrequency(20000.0f);
        }
    }

    bool isFilterEnvelopeEnabled() const { return filterEnvEnabled; }

    void setFilterEnvelopeParams(float initFreq, float endFreq, float attack)
    {
        filterEnvInitFreq = juce::jlimit(200.0f, 20000.0f, initFreq);
        filterEnvEndFreq = juce::jlimit(200.0f, 20000.0f, endFreq);
        filterEnvAttack = juce::jlimit(0.0f, 1.0f, attack);
    }

    // Reset filter envelope position (call when new note/trigger)
    void resetFilterEnvelope()
    {
        filterEnvPosition = 0;
    }

    int getLatencyInSamples() const
    {
        return convolution.getLatency();
    }

    const juce::AudioBuffer<float>& getOriginalIR() const { return originalIR; }

    // Returns a copy of the processed IR buffer (thread-safe for UI access)
    juce::AudioBuffer<float> getProcessedIRCopy() const
    {
        std::lock_guard<std::mutex> lock(rebuildMutex);
        juce::AudioBuffer<float> copy;
        copy.makeCopyOf(processedIR);
        return copy;
    }

    float getIRLengthSeconds() const
    {
        if (originalIR.getNumSamples() == 0 || originalSampleRate <= 0)
            return 0.0f;
        return static_cast<float>(originalIR.getNumSamples()) / static_cast<float>(originalSampleRate);
    }

    // Update envelope parameters from UI (call from message thread)
    void setEnvelopeParameters(float attack, float decay, float length)
    {
        if (std::abs(attack - cachedAttack) > 0.001f ||
            std::abs(decay - cachedDecay) > 0.001f ||
            std::abs(length - cachedLength) > 0.001f)
        {
            cachedAttack = attack;
            cachedDecay = decay;
            cachedLength = length;
            rebuildProcessedIR();
        }
    }

private:
    juce::dsp::Convolution convolution;
    juce::dsp::ProcessSpec currentSpec;

    juce::AudioBuffer<float> originalIR;
    juce::AudioBuffer<float> processedIR;

    double originalSampleRate = 44100.0;
    double targetSampleRate = 44100.0;

    bool reversed = false;
    bool useZeroLatency = true;

    // New features
    float irOffset = 0.0f;                    // IR start offset (0-0.5)
    Quality quality = Quality::Medium;        // Sample rate quality
    StereoMode stereoMode = StereoMode::TrueStereo;  // Stereo processing mode
    bool volumeCompensation = true;           // Auto-level matching

    // Filter envelope
    bool filterEnvEnabled = false;
    float filterEnvInitFreq = 20000.0f;       // Initial cutoff frequency
    float filterEnvEndFreq = 2000.0f;         // End cutoff frequency
    float filterEnvAttack = 0.3f;             // Attack time (0-1, percentage of IR)
    juce::dsp::StateVariableTPTFilter<float> filterEnvFilter;
    int filterEnvPosition = 0;                // Current position in samples
    int filterEnvTotalSamples = 0;            // Total samples for envelope

    // Transient detection for filter envelope auto-reset
    float transientThreshold = 0.05f;         // Threshold for transient detection
    float previousInputLevel = 0.0f;          // For detecting level rises
    int silenceSampleCount = 0;               // Count samples of silence
    static constexpr int silenceThresholdSamples = 2048;  // ~46ms at 44.1kHz

    // Cached envelope parameters (used during rebuild)
    float cachedAttack = 0.0f;
    float cachedDecay = 1.0f;
    float cachedLength = 1.0f;

    // Atomic flags for deferred rebuild (real-time safe communication)
    std::atomic<bool> needsRebuild{false};
    std::atomic<float> pendingAttack{0.0f};
    std::atomic<float> pendingDecay{1.0f};
    std::atomic<float> pendingLength{1.0f};

    // Mutex to protect rebuildProcessedIR from concurrent access
    // Mutable to allow locking in const methods (getProcessedIRCopy)
    mutable std::mutex rebuildMutex;

    bool envelopeChanged(const EnvelopeProcessor& envelope) const
    {
        return std::abs(envelope.getAttack() - cachedAttack) > 0.001f ||
               std::abs(envelope.getDecay() - cachedDecay) > 0.001f ||
               std::abs(envelope.getLength() - cachedLength) > 0.001f;
    }

    void rebuildProcessedIR()
    {
        std::lock_guard<std::mutex> lock(rebuildMutex);

        if (originalIR.getNumSamples() == 0)
            return;

        int originalLength = originalIR.getNumSamples();

        // Calculate start offset (skip beginning of IR)
        int startOffset = static_cast<int>(originalLength * irOffset);
        startOffset = juce::jlimit(0, originalLength - 64, startOffset);

        // Calculate length after truncation (relative to remaining IR after offset)
        int remainingLength = originalLength - startOffset;
        int newLength = static_cast<int>(remainingLength * cachedLength);
        newLength = std::max(64, newLength); // Minimum IR length

        // Apply quality-based sample rate adjustment
        double effectiveSampleRate = originalSampleRate;
        int sampleRateDivisor = 1;
        switch (quality)
        {
            case Quality::LoFi:
                sampleRateDivisor = 4;
                effectiveSampleRate = originalSampleRate / 4.0;
                break;
            case Quality::Low:
                sampleRateDivisor = 2;
                effectiveSampleRate = originalSampleRate / 2.0;
                break;
            case Quality::Medium:
            case Quality::High:
            default:
                sampleRateDivisor = 1;
                effectiveSampleRate = originalSampleRate;
                break;
        }

        // Adjust length for quality (lower quality = longer perceived reverb)
        int qualityAdjustedLength = newLength / sampleRateDivisor;
        qualityAdjustedLength = std::max(64, qualityAdjustedLength);

        // Create processed IR buffer
        processedIR.setSize(originalIR.getNumChannels(), qualityAdjustedLength);

        // Copy and process with offset and quality resampling
        for (int channel = 0; channel < originalIR.getNumChannels(); ++channel)
        {
            const float* srcData = originalIR.getReadPointer(channel);
            float* destData = processedIR.getWritePointer(channel);

            for (int i = 0; i < qualityAdjustedLength; ++i)
            {
                // Calculate source index with offset and quality resampling
                int baseIndex = i * sampleRateDivisor;
                int srcIndex;

                if (reversed)
                {
                    // Reverse: read from end backwards, but start from offset
                    srcIndex = (originalLength - 1 - startOffset) - baseIndex;
                }
                else
                {
                    // Normal: start from offset
                    srcIndex = startOffset + baseIndex;
                }

                srcIndex = juce::jlimit(0, originalLength - 1, srcIndex);
                destData[i] = srcData[srcIndex];
            }
        }

        // Apply mono-to-stereo mode if requested (sum stereo IR to mono)
        if (stereoMode == StereoMode::MonoToStereo && processedIR.getNumChannels() > 1)
        {
            // Sum channels to mono
            juce::AudioBuffer<float> monoIR(1, qualityAdjustedLength);
            monoIR.clear();
            for (int channel = 0; channel < processedIR.getNumChannels(); ++channel)
            {
                monoIR.addFrom(0, 0, processedIR, channel, 0, qualityAdjustedLength,
                               1.0f / processedIR.getNumChannels());
            }
            processedIR = std::move(monoIR);
        }

        // Apply envelope
        applyEnvelope(processedIR);

        // Apply volume compensation if enabled
        if (volumeCompensation)
        {
            applyVolumeCompensation(processedIR);
        }

        // Update filter envelope total samples
        filterEnvTotalSamples = static_cast<int>(qualityAdjustedLength * (targetSampleRate / effectiveSampleRate));

        // Load into convolution engine
        auto trimMode = juce::dsp::Convolution::Trim::no; // We handle length ourselves
        auto juceStereoMode = processedIR.getNumChannels() > 1
                              ? juce::dsp::Convolution::Stereo::yes
                              : juce::dsp::Convolution::Stereo::no;

        // Copy the buffer since loadImpulseResponse takes ownership
        juce::AudioBuffer<float> irCopy;
        irCopy.makeCopyOf(processedIR);

        convolution.loadImpulseResponse(std::move(irCopy),
                                        effectiveSampleRate,
                                        juceStereoMode,
                                        trimMode,
                                        juce::dsp::Convolution::Normalise::yes);
    }

    void applyVolumeCompensation(juce::AudioBuffer<float>& buffer)
    {
        // Calculate RMS of the IR
        float sumSquares = 0.0f;
        int totalSamples = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float* data = buffer.getReadPointer(channel);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                sumSquares += data[i] * data[i];
                ++totalSamples;
            }
        }

        if (totalSamples == 0)
            return;

        float rms = std::sqrt(sumSquares / static_cast<float>(totalSamples));

        // Target RMS (normalized to a reasonable level)
        const float targetRMS = 0.1f;

        if (rms > 1e-6f)
        {
            float gain = targetRMS / rms;
            // Limit gain to reasonable range
            gain = juce::jlimit(0.1f, 10.0f, gain);

            buffer.applyGain(gain);
        }
    }

    void applyEnvelope(juce::AudioBuffer<float>& buffer)
    {
        int numSamples = buffer.getNumSamples();
        if (numSamples == 0)
            return;

        // Attack: fade in at the beginning
        // Attack parameter 0-1 maps to 0-500ms attack time
        float attackTimeSec = cachedAttack * 0.5f;
        int attackSamples = static_cast<int>(attackTimeSec * originalSampleRate);
        attackSamples = std::min(attackSamples, numSamples);

        // Decay: controls the decay curve shape
        // Decay = 1.0 means natural decay, decay = 0 means immediate cutoff after attack
        float decayFactor = cachedDecay;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            float* data = buffer.getWritePointer(channel);

            for (int i = 0; i < numSamples; ++i)
            {
                float envelope = 1.0f;
                float position = static_cast<float>(i) / static_cast<float>(numSamples);

                // Attack phase (fade in)
                if (i < attackSamples && attackSamples > 0)
                {
                    float attackProgress = static_cast<float>(i) / static_cast<float>(attackSamples);
                    // Smooth attack curve
                    envelope *= 0.5f * (1.0f - std::cos(attackProgress * juce::MathConstants<float>::pi));
                }

                // Decay phase (modify natural decay)
                if (i >= attackSamples && decayFactor < 1.0f)
                {
                    float attackRatio = static_cast<float>(attackSamples) / numSamples;
                    float decayDenominator = 1.0f - attackRatio;
                    
                    if (decayDenominator < 0.001f)
                    {
                        // Attack consumed entire IR, skip decay shaping
                        // but still apply the current envelope value
                        data[i] *= envelope;
                        continue;
                    }
                    
                    float decayPosition = (position - attackRatio) / decayDenominator;
                    decayPosition = std::max(0.0f, decayPosition);                    // Apply decay shaping - lower decay values cause faster fadeout
                    float decayEnvelope = std::pow(1.0f - decayPosition, 2.0f - decayFactor * 2.0f);
                    envelope *= juce::jlimit(0.0f, 1.0f, decayFactor + (1.0f - decayFactor) * decayEnvelope);
                }

                data[i] *= envelope;
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConvolutionEngine)
};

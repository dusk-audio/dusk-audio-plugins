/*
  ==============================================================================

    Convolution Reverb - Convolution Engine
    Wrapper around juce::dsp::Convolution with envelope and reverse support
    Copyright (c) 2025 Luna Co. Audio

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
    ConvolutionEngine() = default;
    ~ConvolutionEngine() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSpec = spec;
        convolution.prepare(spec);
        convolution.reset();
    }

    void reset()
    {
        convolution.reset();
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
    void processBlock(juce::AudioBuffer<float>& buffer, const EnvelopeProcessor& envelope)
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

        // Process convolution (real-time safe)
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        convolution.process(context);
    }

    void processBlock(juce::AudioBuffer<float>& buffer)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        convolution.process(context);
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

        // Calculate length after truncation
        int originalLength = originalIR.getNumSamples();
        int newLength = static_cast<int>(originalLength * cachedLength);
        newLength = std::max(64, newLength); // Minimum IR length

        // Create processed IR buffer
        processedIR.setSize(originalIR.getNumChannels(), newLength);

        // Copy and process
        for (int channel = 0; channel < originalIR.getNumChannels(); ++channel)
        {
            const float* srcData = originalIR.getReadPointer(channel);
            float* destData = processedIR.getWritePointer(channel);

            for (int i = 0; i < newLength; ++i)
            {
                int srcIndex = reversed ? (originalLength - 1 - i) : i;
                srcIndex = juce::jlimit(0, originalLength - 1, srcIndex);
                destData[i] = srcData[srcIndex];
            }
        }

        // Apply envelope
        applyEnvelope(processedIR);

        // Load into convolution engine
        auto trimMode = juce::dsp::Convolution::Trim::no; // We handle length ourselves
        auto stereoMode = processedIR.getNumChannels() > 1
                              ? juce::dsp::Convolution::Stereo::yes
                              : juce::dsp::Convolution::Stereo::no;

        // Copy the buffer since loadImpulseResponse takes ownership
        juce::AudioBuffer<float> irCopy;
        irCopy.makeCopyOf(processedIR);

        convolution.loadImpulseResponse(std::move(irCopy),
                                        originalSampleRate,
                                        stereoMode,
                                        trimMode,
                                        juce::dsp::Convolution::Normalise::yes);
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

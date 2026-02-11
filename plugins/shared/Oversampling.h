/*
  ==============================================================================

    Oversampling.h
    Shared high-quality oversampling for Dusk Audio plugins

    Uses FIR equiripple filters for superior alias rejection, essential for
    saturation and other nonlinear processing.

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <atomic>

namespace DuskAudio
{

//==============================================================================
/**
    High-quality oversampling manager with 2x/4x selection.

    Uses FIR equiripple filters which provide superior alias rejection compared
    to IIR filters. This is essential for saturation, tape emulation, and other
    nonlinear processing where harmonic content can fold back into the audio band.

    Usage:
        // In your processor header:
        DuskAudio::OversamplingManager oversampling;

        // In prepareToPlay:
        oversampling.prepare(sampleRate, samplesPerBlock, numChannels);
        setLatencySamples(oversampling.getLatencyInSamples());

        // In processBlock:
        auto factor = oversampling.getOversamplingFactor();
        auto* oversampledBuffer = oversampling.processSamplesUp(buffer);

        // ... process at oversampled rate ...

        oversampling.processSamplesDown(buffer);
*/
class OversamplingManager
{
public:
    enum class Quality
    {
        x2 = 0,    // 2x oversampling (1 stage)
        x4 = 1     // 4x oversampling (2 stages)
    };

    OversamplingManager() = default;
    ~OversamplingManager() = default;

    //==============================================================================
    /** Prepares the oversampling for processing.
        Call this from your prepareToPlay() method.

        @param sampleRate       The base sample rate
        @param samplesPerBlock  The maximum block size
        @param numChannels      Number of audio channels
    */
    void prepare(double sampleRate, int samplesPerBlock, int numChannels)
    {
        jassert(samplesPerBlock > 0 && numChannels > 0);
        if (samplesPerBlock <= 0 || numChannels <= 0)
            return;

        // Check if we need to recreate (sample rate, channels, or block size changed)
        bool needsRecreate = (std::abs(sampleRate - lastSampleRate) > 0.01) ||
                             (numChannels != lastNumChannels) ||
                             (samplesPerBlock != lastBlockSize) ||
                             !oversampler2x || !oversampler4x;

        if (needsRecreate)
        {
            // Use FIR equiripple filters for superior anti-aliasing
            // This provides better alias rejection than IIR, essential for saturation
            oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
                static_cast<size_t>(numChannels), 1,
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);

            oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
                static_cast<size_t>(numChannels), 2,
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);

            oversampler2x->initProcessing(static_cast<size_t>(samplesPerBlock));
            oversampler4x->initProcessing(static_cast<size_t>(samplesPerBlock));

            lastSampleRate = sampleRate;
            lastNumChannels = numChannels;
            lastBlockSize = samplesPerBlock;
        }
        else
        {
            oversampler2x->reset();
            oversampler4x->reset();
        }

        baseSampleRate = sampleRate;
        baseBlockSize = samplesPerBlock;
    }

    /** Resets the oversampling filters. Call when playback stops. */
    void reset()
    {
        if (oversampler2x) oversampler2x->reset();
        if (oversampler4x) oversampler4x->reset();
    }

    //==============================================================================
    /** Sets the oversampling quality (2x or 4x).
        @param quality  The desired oversampling quality
    */
    void setQuality(Quality quality)
    {
        currentQuality = quality;
    }

    /** Sets the oversampling factor directly (2 or 4).
        @param factor  2 for 2x, 4 for 4x oversampling
    */
    void setFactor(int factor)
    {
        currentQuality = (factor >= 4) ? Quality::x4 : Quality::x2;
    }

    /** Gets the current oversampling factor (2 or 4). */
    int getOversamplingFactor() const
    {
        return (currentQuality == Quality::x4) ? 4 : 2;
    }

    /** Gets the effective sample rate after oversampling. */
    double getOversampledSampleRate() const
    {
        return baseSampleRate * getOversamplingFactor();
    }

    /** Gets the latency introduced by oversampling in samples. */
    int getLatencyInSamples() const
    {
        if (currentQuality == Quality::x4 && oversampler4x)
            return static_cast<int>(oversampler4x->getLatencyInSamples());
        else if (oversampler2x)
            return static_cast<int>(oversampler2x->getLatencyInSamples());
        return 0;
    }

    //==============================================================================
    /** Upsamples the input buffer.
        @param inputBlock  The audio block to upsample
        @return            Reference to the oversampled audio block
    */
    juce::dsp::AudioBlock<float> processSamplesUp(juce::dsp::AudioBlock<float>& inputBlock)
    {
        if (currentQuality == Quality::x4 && oversampler4x)
            return oversampler4x->processSamplesUp(inputBlock);
        else if (oversampler2x)
            return oversampler2x->processSamplesUp(inputBlock);

        return inputBlock;
    }

    /** Upsamples from an AudioBuffer.
        @param buffer  The audio buffer to upsample
        @return        Reference to the oversampled audio block
    */
    juce::dsp::AudioBlock<float> processSamplesUp(juce::AudioBuffer<float>& buffer)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        return processSamplesUp(block);
    }

    /** Downsamples the processed audio back to the original sample rate.
        @param outputBlock  The audio block to write the downsampled result to
    */
    void processSamplesDown(juce::dsp::AudioBlock<float>& outputBlock)
    {
        if (currentQuality == Quality::x4 && oversampler4x)
            oversampler4x->processSamplesDown(outputBlock);
        else if (oversampler2x)
            oversampler2x->processSamplesDown(outputBlock);
    }

    /** Downsamples to an AudioBuffer.
        @param buffer  The audio buffer to write the downsampled result to
    */
    void processSamplesDown(juce::AudioBuffer<float>& buffer)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        processSamplesDown(block);
    }

    //==============================================================================
    /** Creates a parameter layout for oversampling selection.
        Add this to your plugin's parameter layout.

        @param paramID  The parameter ID (default: "oversampling")
        @param name     The parameter name (default: "Oversampling")
        @return         A unique_ptr to the parameter
    */
    static std::unique_ptr<juce::AudioParameterChoice> createParameter(
        const juce::String& paramID = "oversampling",
        const juce::String& name = "Oversampling")
    {
        return std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(paramID, 1),
            name,
            juce::StringArray("2x", "4x"),
            0);  // Default to 2x
    }

    /** Updates the oversampling from a parameter value.
        @param paramValue  The parameter value (0 = 2x, 1 = 4x)
    */
    void updateFromParameter(float paramValue)
    {
        setFactor(paramValue >= 0.5f ? 4 : 2);
    }

    /** Checks if the oversampling is properly initialized. */
    bool isInitialized() const
    {
        return oversampler2x != nullptr && oversampler4x != nullptr;
    }

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;

    Quality currentQuality = Quality::x2;
    double baseSampleRate = 44100.0;
    int baseBlockSize = 512;

    // For change detection
    double lastSampleRate = 0.0;
    int lastNumChannels = 0;
    int lastBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OversamplingManager)
};

} // namespace DuskAudio

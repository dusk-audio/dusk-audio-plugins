#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <mutex>

class CabinetProcessor
{
public:
    CabinetProcessor() = default;
    ~CabinetProcessor() = default;

    void prepare(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 2;

        convolution.prepare(spec);
        convolution.reset();

        currentSampleRate = sampleRate;

        dryBuffer.setSize(2, samplesPerBlock);
    }

    void reset()
    {
        convolution.reset();
    }

    bool loadIR(const juce::File& irFile)
    {
        if (!irFile.existsAsFile())
            return false;

        // Load IR file
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(irFile));

        if (!reader)
            return false;

        // Read IR data
        juce::AudioBuffer<float> irBuffer(
            static_cast<int>(reader->numChannels),
            static_cast<int>(reader->lengthInSamples));

        reader->read(&irBuffer, 0,
                     static_cast<int>(reader->lengthInSamples),
                     0, true, true);

        // Store IR sample rate
        double irSampleRate = reader->sampleRate;

        // Store IR name
        irName = irFile.getFileNameWithoutExtension();
        irPath = irFile.getFullPathName();

        // Load into convolution engine
        convolution.loadImpulseResponse(
            std::move(irBuffer),
            irSampleRate,
            juce::dsp::Convolution::Stereo::yes,
            juce::dsp::Convolution::Trim::yes,
            juce::dsp::Convolution::Normalise::yes);

        irLoaded.store(true);
        return true;
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        if (!irLoaded.load())
            return;

        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();

        // Store dry signal for mixing
        if (mix < 0.99f)
        {
            for (int ch = 0; ch < numChannels && ch < 2; ++ch)
            {
                dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
            }
        }

        // Process through convolution
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        convolution.process(context);

        // Mix dry/wet
        if (mix < 0.99f)
        {
            float wetGain = mix;
            float dryGain = 1.0f - mix;

            for (int ch = 0; ch < numChannels && ch < 2; ++ch)
            {
                buffer.applyGain(ch, 0, numSamples, wetGain);
                buffer.addFrom(ch, 0, dryBuffer, ch, 0, numSamples, dryGain);
            }
        }
    }

    void setMix(float newMix)
    {
        mix = juce::jlimit(0.0f, 1.0f, newMix);
    }

    float getMix() const { return mix; }

    bool isIRLoaded() const { return irLoaded.load(); }

    juce::String getIRName() const { return irName; }
    juce::String getIRPath() const { return irPath; }

    int getLatencyInSamples() const
    {
        return convolution.getLatency();
    }

private:
    juce::dsp::Convolution convolution;

    double currentSampleRate = 44100.0;
    float mix = 1.0f;

    std::atomic<bool> irLoaded{false};
    juce::String irName{"No IR"};
    juce::String irPath;

    juce::AudioBuffer<float> dryBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CabinetProcessor)
};

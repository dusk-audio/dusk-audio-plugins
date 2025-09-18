#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

class HarmonicGeneratorAudioProcessor : public juce::AudioProcessor
{
public:
    HarmonicGeneratorAudioProcessor()
        : AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          oversampling(2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR)
    {
        addParameter(oversamplingSwitch = new juce::AudioParameterBool("oversampling", "Oversampling", true));

        auto harmonicRange = juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f);
        harmonicRange.setSkewForCentre(0.10f);

        addParameter(secondHarmonic = new juce::AudioParameterFloat("secondHarmonic", "2nd Harmonic", harmonicRange, 0.0f));
        addParameter(thirdHarmonic  = new juce::AudioParameterFloat("thirdHarmonic", "3rd Harmonic", harmonicRange, 0.0f));
        addParameter(wetDryMix = new juce::AudioParameterFloat("wetDryMix", "Wet/Dry Mix", 0.0f, 1.0f, 1.0f));

    }

    ~HarmonicGeneratorAudioProcessor() override {}

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        oversampling.initProcessing(static_cast<size_t>(samplesPerBlock));
        oversampling.reset();
        lastSampleRate = sampleRate;

        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 10.0); //20.0 = 20Hz, adjust to taste
        highPassFilterL.state = *coeffs;
        highPassFilterR.state = *coeffs;

        highPassFilterL.prepare({ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1 });
        highPassFilterR.prepare({ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1 });

        highPassFilterL.reset();
        highPassFilterR.reset();
    }

    void releaseResources() override
    {
        oversampling.reset();
    }

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
               layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/) override
    {
        juce::ScopedNoDenormals noDenormals;
        auto totalNumInputChannels = getTotalNumInputChannels();
        auto totalNumOutputChannels = getTotalNumOutputChannels();

        for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
            buffer.clear(i, 0, buffer.getNumSamples());

        dryBuffer.makeCopyOf(buffer);

        if (*oversamplingSwitch)
        {
            juce::dsp::AudioBlock<float> block(buffer);
            auto oversampledBlock = oversampling.processSamplesUp(block);
            processHarmonics(oversampledBlock);
            oversampling.processSamplesDown(block);
        }
        else
        {
            juce::dsp::AudioBlock<float> block(buffer);
            processHarmonics(block);
        }

        float wet = *wetDryMix;
        float dry = 1.0f - wet;

        for (int channel = 0; channel < totalNumOutputChannels; ++channel)
        {
            auto* outputData = buffer.getWritePointer(channel);
            auto* dryData = dryBuffer.getReadPointer(channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                outputData[sample] = outputData[sample] * wet + dryData[sample] * dry;
            }
        }
    }

    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& /*midiMessages*/) override
    {
        juce::AudioBuffer<float> floatBuffer;
        floatBuffer.makeCopyOf(buffer);
        juce::MidiBuffer dummy;
        processBlock(floatBuffer, dummy);
        buffer.makeCopyOf(floatBuffer);
    }

    juce::AudioProcessorEditor* createEditor() override
    {
        return new juce::GenericAudioProcessorEditor(*this);
    }

    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "HarmonicGenerator"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override
    {
        juce::MemoryOutputStream stream(destData, true);
        stream.writeFloat(*secondHarmonic);
        stream.writeFloat(*thirdHarmonic);
        stream.writeFloat(*wetDryMix);
        stream.writeBool(*oversamplingSwitch);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
         juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        *secondHarmonic = stream.readFloat();
        *thirdHarmonic = stream.readFloat();
        *wetDryMix = stream.readFloat();
        *oversamplingSwitch = stream.readBool();
    }

private:
    juce::AudioParameterBool* oversamplingSwitch;
    juce::AudioParameterFloat* secondHarmonic;
    juce::AudioParameterFloat* thirdHarmonic;
    juce::AudioParameterFloat* wetDryMix;
    juce::dsp::Oversampling<float> oversampling;
    juce::AudioBuffer<float> dryBuffer;
    double lastSampleRate = 0.0;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> highPassFilterL, highPassFilterR;

    void processHarmonics(juce::dsp::AudioBlock<float>& block)
    {
        const float secondGain = *secondHarmonic * 0.25f;
        const float thirdGain  = *thirdHarmonic * 1.5f;
        const float silenceThreshold = 1.0e-4f;
        const float envelopeAttack = 0.005f;

        const size_t numChannels = block.getNumChannels();
        const size_t numSamples = block.getNumSamples();

        float envelopes[2] = { 0.0f, 0.0f };

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* data = block.getChannelPointer(channel);

            for (size_t sample = 0; sample < numSamples; ++sample)
            {
                float x = data[sample];
                float out = x;

                float amplitude = std::abs(x);
                envelopes[channel] = (1.0f - envelopeAttack) * envelopes[channel] + envelopeAttack * amplitude;

                if (envelopes[channel] > silenceThreshold)
                {
                    float secondOrder = std::tanh(x + 0.3f * x * x) - std::tanh(x);
                    float thirdOrder  = x * x * x;

                    float harmonicSum = secondGain * secondOrder + thirdGain * thirdOrder;

                    out = x + harmonicSum;
                }

                data[sample] = juce::jlimit(-1.0f, 1.0f, out);
            }
        }

        auto left  = block.getSingleChannelBlock(0);
        auto right = block.getSingleChannelBlock(1);

        highPassFilterL.process(juce::dsp::ProcessContextReplacing<float>(left));
        highPassFilterR.process(juce::dsp::ProcessContextReplacing<float>(right));
    }


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicGeneratorAudioProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonicGeneratorAudioProcessor();
}

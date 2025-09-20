#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

HarmonicGeneratorAudioProcessor::HarmonicGeneratorAudioProcessor()
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
    addParameter(fourthHarmonic = new juce::AudioParameterFloat("fourthHarmonic", "4th Harmonic", harmonicRange, 0.0f));
    addParameter(fifthHarmonic  = new juce::AudioParameterFloat("fifthHarmonic", "5th Harmonic", harmonicRange, 0.0f));

    addParameter(evenHarmonics = new juce::AudioParameterFloat("evenHarmonics", "Even Harmonics", 0.0f, 1.0f, 0.5f));
    addParameter(oddHarmonics = new juce::AudioParameterFloat("oddHarmonics", "Odd Harmonics", 0.0f, 1.0f, 0.5f));

    addParameter(warmth = new juce::AudioParameterFloat("warmth", "Warmth", 0.0f, 1.0f, 0.5f));
    addParameter(brightness = new juce::AudioParameterFloat("brightness", "Brightness", 0.0f, 1.0f, 0.5f));

    addParameter(drive = new juce::AudioParameterFloat("drive", "Drive",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f));
    addParameter(outputGain = new juce::AudioParameterFloat("outputGain", "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));

    addParameter(wetDryMix = new juce::AudioParameterFloat("wetDryMix", "Wet/Dry Mix", 0.0f, 1.0f, 1.0f));
}

HarmonicGeneratorAudioProcessor::~HarmonicGeneratorAudioProcessor()
{
}

void HarmonicGeneratorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    oversampling.initProcessing(static_cast<size_t>(samplesPerBlock));
    oversampling.reset();
    lastSampleRate = sampleRate;

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 10.0);
    highPassFilterL.state = *coeffs;
    highPassFilterR.state = *coeffs;

    highPassFilterL.prepare({ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1 });
    highPassFilterR.prepare({ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1 });

    highPassFilterL.reset();
    highPassFilterR.reset();
}

void HarmonicGeneratorAudioProcessor::releaseResources()
{
    oversampling.reset();
}

bool HarmonicGeneratorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void HarmonicGeneratorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Calculate input levels
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        peakL = juce::jmax(peakL, std::abs(buffer.getSample(0, i)));
        if (totalNumOutputChannels > 1)
            peakR = juce::jmax(peakR, std::abs(buffer.getSample(1, i)));
    }

    const float attackTime = 0.3f;
    const float releaseTime = 0.7f;

    inputLevelL = inputLevelL < peakL ?
        inputLevelL + (peakL - inputLevelL) * attackTime :
        inputLevelL + (peakL - inputLevelL) * (1.0f - releaseTime);
    inputLevelR = inputLevelR < peakR ?
        inputLevelR + (peakR - inputLevelR) * attackTime :
        inputLevelR + (peakR - inputLevelR) * (1.0f - releaseTime);

    dryBuffer.makeCopyOf(buffer);

    // Apply input drive
    float driveGain = juce::Decibels::decibelsToGain(drive->get());
    buffer.applyGain(driveGain);

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

    // Apply output gain
    float outGain = juce::Decibels::decibelsToGain(outputGain->get());
    buffer.applyGain(outGain);

    // Mix dry/wet
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

    // Calculate output levels
    peakL = peakR = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        peakL = juce::jmax(peakL, std::abs(buffer.getSample(0, i)));
        if (totalNumOutputChannels > 1)
            peakR = juce::jmax(peakR, std::abs(buffer.getSample(1, i)));
    }

    outputLevelL = outputLevelL < peakL ?
        outputLevelL + (peakL - outputLevelL) * attackTime :
        outputLevelL + (peakL - outputLevelL) * (1.0f - releaseTime);
    outputLevelR = outputLevelR < peakR ?
        outputLevelR + (peakR - outputLevelR) * attackTime :
        outputLevelR + (peakR - outputLevelR) * (1.0f - releaseTime);
}

void HarmonicGeneratorAudioProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::AudioBuffer<float> floatBuffer(buffer.getNumChannels(), buffer.getNumSamples());

    // Convert double to float
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* src = buffer.getReadPointer(channel);
        auto* dst = floatBuffer.getWritePointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            dst[i] = static_cast<float>(src[i]);
    }

    processBlock(floatBuffer, midiMessages);

    // Convert float back to double
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* src = floatBuffer.getReadPointer(channel);
        auto* dst = buffer.getWritePointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            dst[i] = static_cast<double>(src[i]);
    }
}

void HarmonicGeneratorAudioProcessor::processHarmonics(juce::dsp::AudioBlock<float>& block)
{
    auto* leftChannel = block.getChannelPointer(0);
    auto* rightChannel = block.getNumChannels() > 1 ? block.getChannelPointer(1) : nullptr;

    float second = secondHarmonic->get();
    float third = thirdHarmonic->get();
    float fourth = fourthHarmonic->get();
    float fifth = fifthHarmonic->get();

    float evenMix = evenHarmonics->get();
    float oddMix = oddHarmonics->get();
    float warmthAmount = warmth->get();
    float brightnessAmount = brightness->get();

    for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
    {
        // Process left channel
        float inputL = leftChannel[sample];
        float processedL = generateHarmonics(inputL,
            second * evenMix * (1.0f + warmthAmount),
            third * oddMix * (1.0f + brightnessAmount * 0.5f),
            fourth * evenMix * warmthAmount,
            fifth * oddMix * brightnessAmount);

        // Simply store the processed sample (filtering is handled at channel level)
        leftChannel[sample] = processedL;

        // Process right channel if stereo
        if (rightChannel != nullptr)
        {
            float inputR = rightChannel[sample];
            float processedR = generateHarmonics(inputR,
                second * evenMix * (1.0f + warmthAmount),
                third * oddMix * (1.0f + brightnessAmount * 0.5f),
                fourth * evenMix * warmthAmount,
                fifth * oddMix * brightnessAmount);

            rightChannel[sample] = processedR;
        }
    }

    // Apply high-pass filter to entire block to remove DC
    if (block.getNumChannels() >= 1)
    {
        auto leftBlock = block.getSingleChannelBlock(0);
        juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
        highPassFilterL.process(leftContext);
    }

    if (block.getNumChannels() >= 2)
    {
        auto rightBlock = block.getSingleChannelBlock(1);
        juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);
        highPassFilterR.process(rightContext);
    }
}

float HarmonicGeneratorAudioProcessor::generateHarmonics(float input, float second, float third, float fourth, float fifth)
{
    // Soft clipping for analog-style saturation
    float x = input;
    float x2 = x * x;
    float x3 = x2 * x;
    float x4 = x2 * x2;
    float x5 = x4 * x;

    // Generate harmonics with phase-aligned synthesis
    float output = input;

    // 2nd harmonic (even - warmth)
    output += second * 0.5f * x2 * (input >= 0 ? 1.0f : -1.0f);

    // 3rd harmonic (odd - presence)
    output += third * 0.3f * x3;

    // 4th harmonic (even - body)
    output += fourth * 0.2f * x4 * (input >= 0 ? 1.0f : -1.0f);

    // 5th harmonic (odd - edge)
    output += fifth * 0.15f * x5;

    // Soft limiting
    output = std::tanh(output * 0.7f) * 1.43f;

    return output;
}

juce::AudioProcessorEditor* HarmonicGeneratorAudioProcessor::createEditor()
{
    return new HarmonicGeneratorAudioProcessorEditor(*this);
}

void HarmonicGeneratorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("HarmonicGeneratorState");

    state.setProperty("oversampling", oversamplingSwitch->get(), nullptr);
    state.setProperty("secondHarmonic", secondHarmonic->get(), nullptr);
    state.setProperty("thirdHarmonic", thirdHarmonic->get(), nullptr);
    state.setProperty("fourthHarmonic", fourthHarmonic->get(), nullptr);
    state.setProperty("fifthHarmonic", fifthHarmonic->get(), nullptr);
    state.setProperty("evenHarmonics", evenHarmonics->get(), nullptr);
    state.setProperty("oddHarmonics", oddHarmonics->get(), nullptr);
    state.setProperty("warmth", warmth->get(), nullptr);
    state.setProperty("brightness", brightness->get(), nullptr);
    state.setProperty("drive", drive->get(), nullptr);
    state.setProperty("outputGain", outputGain->get(), nullptr);
    state.setProperty("wetDryMix", wetDryMix->get(), nullptr);

    juce::MemoryOutputStream stream(destData, false);
    state.writeToStream(stream);
}

void HarmonicGeneratorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ValueTree state = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));

    if (state.isValid())
    {
        oversamplingSwitch->setValueNotifyingHost(state.getProperty("oversampling", true));
        secondHarmonic->setValueNotifyingHost(state.getProperty("secondHarmonic", 0.0f));
        thirdHarmonic->setValueNotifyingHost(state.getProperty("thirdHarmonic", 0.0f));
        fourthHarmonic->setValueNotifyingHost(state.getProperty("fourthHarmonic", 0.0f));
        fifthHarmonic->setValueNotifyingHost(state.getProperty("fifthHarmonic", 0.0f));
        evenHarmonics->setValueNotifyingHost(state.getProperty("evenHarmonics", 0.5f));
        oddHarmonics->setValueNotifyingHost(state.getProperty("oddHarmonics", 0.5f));
        warmth->setValueNotifyingHost(state.getProperty("warmth", 0.5f));
        brightness->setValueNotifyingHost(state.getProperty("brightness", 0.5f));
        drive->setValueNotifyingHost(state.getProperty("drive", 0.0f));
        outputGain->setValueNotifyingHost(state.getProperty("outputGain", 0.0f));
        wetDryMix->setValueNotifyingHost(state.getProperty("wetDryMix", 1.0f));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonicGeneratorAudioProcessor();
}
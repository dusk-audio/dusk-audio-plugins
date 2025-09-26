#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

HarmonicGeneratorAudioProcessor::HarmonicGeneratorAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "HarmonicGenerator", createParameterLayout()),
      oversampling(2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR)
{
    // Get parameter pointers from APVTS for fast access during processing
    oversamplingSwitch = apvts.getRawParameterValue("oversampling");
    secondHarmonic = apvts.getRawParameterValue("secondHarmonic");
    thirdHarmonic = apvts.getRawParameterValue("thirdHarmonic");
    fourthHarmonic = apvts.getRawParameterValue("fourthHarmonic");
    fifthHarmonic = apvts.getRawParameterValue("fifthHarmonic");
    evenHarmonics = apvts.getRawParameterValue("evenHarmonics");
    oddHarmonics = apvts.getRawParameterValue("oddHarmonics");
    warmth = apvts.getRawParameterValue("warmth");
    brightness = apvts.getRawParameterValue("brightness");
    drive = apvts.getRawParameterValue("drive");
    outputGain = apvts.getRawParameterValue("outputGain");
    wetDryMix = apvts.getRawParameterValue("wetDryMix");

    // Validate all parameters are initialized - critical for release builds
    if (!oversamplingSwitch || !secondHarmonic || !thirdHarmonic ||
        !fourthHarmonic || !fifthHarmonic || !evenHarmonics ||
        !oddHarmonics || !warmth || !brightness || !drive ||
        !outputGain || !wetDryMix)
    {
        // Log error for debugging
        DBG("HarmonicGenerator: Failed to initialize one or more parameters");
        jassertfalse;  // Trigger assertion in debug builds

        // In release builds, this prevents crashes but the plugin won't process audio correctly
        // The null checks in processBlock will use safe defaults
    }
}

HarmonicGeneratorAudioProcessor::~HarmonicGeneratorAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout HarmonicGeneratorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Oversampling
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "oversampling", "Oversampling", true));

    // Harmonic controls
    auto harmonicRange = juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f);
    harmonicRange.setSkewForCentre(0.10f);

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "secondHarmonic", "2nd Harmonic", harmonicRange, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "thirdHarmonic", "3rd Harmonic", harmonicRange, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fourthHarmonic", "4th Harmonic", harmonicRange, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fifthHarmonic", "5th Harmonic", harmonicRange, 0.0f));

    // Global harmonic controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evenHarmonics", "Even Harmonics", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "oddHarmonics", "Odd Harmonics", 0.0f, 1.0f, 0.5f));

    // Character controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "warmth", "Warmth", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "brightness", "Brightness", 0.0f, 1.0f, 0.5f));

    // Gain controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "drive", "Drive",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));

    // Mix control
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wetDryMix", "Wet/Dry Mix", 0.0f, 1.0f, 1.0f));

    return { params.begin(), params.end() };
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

    // Apply input drive (with null check)
    float driveGain = drive ? juce::Decibels::decibelsToGain(drive->load()) : 1.0f;
    buffer.applyGain(driveGain);

    if (oversamplingSwitch && oversamplingSwitch->load() > 0.5f)
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

    // Apply output gain (with null check)
    float outGain = outputGain ? juce::Decibels::decibelsToGain(outputGain->load()) : 1.0f;
    buffer.applyGain(outGain);

    // Mix dry/wet (with null check)
    float wet = wetDryMix ? wetDryMix->load() : 1.0f;
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

    // Get parameter values with null checks
    float second = secondHarmonic ? secondHarmonic->load() : 0.0f;
    float third = thirdHarmonic ? thirdHarmonic->load() : 0.0f;
    float fourth = fourthHarmonic ? fourthHarmonic->load() : 0.0f;
    float fifth = fifthHarmonic ? fifthHarmonic->load() : 0.0f;

    float evenMix = evenHarmonics ? evenHarmonics->load() : 0.5f;
    float oddMix = oddHarmonics ? oddHarmonics->load() : 0.5f;
    float warmthAmount = warmth ? warmth->load() : 0.5f;
    float brightnessAmount = brightness ? brightness->load() : 0.5f;

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
    // Use APVTS for state management
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void HarmonicGeneratorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Use APVTS for state management
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonicGeneratorAudioProcessor();
}
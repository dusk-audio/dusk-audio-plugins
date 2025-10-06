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
    hardwareMode = apvts.getRawParameterValue("hardwareMode");
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
    tone = apvts.getRawParameterValue("tone");

    // Validate all parameters are initialized - critical for release builds
    if (!hardwareMode || !oversamplingSwitch || !secondHarmonic || !thirdHarmonic ||
        !fourthHarmonic || !fifthHarmonic || !evenHarmonics ||
        !oddHarmonics || !warmth || !brightness || !drive ||
        !outputGain || !wetDryMix || !tone)
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

    // Hardware Mode Selection
    juce::StringArray hardwareModes;
    hardwareModes.add("Custom");  // Index 0 - uses individual harmonic controls
    hardwareModes.add("Studer A800");
    hardwareModes.add("Ampex ATR-102");
    hardwareModes.add("Tascam Porta");
    hardwareModes.add("Fairchild 670");
    hardwareModes.add("Pultec EQP-1A");
    hardwareModes.add("UA 610");
    hardwareModes.add("Neve 1073");
    hardwareModes.add("API 2500");
    hardwareModes.add("SSL 4000E");
    hardwareModes.add("Culture Vulture");
    hardwareModes.add("Decapitator");
    hardwareModes.add("HG-2 Black Box");

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "hardwareMode", "Hardware Mode", hardwareModes, 0));

    // Oversampling
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "oversampling", "Oversampling", true));

    // Harmonic controls (used in Custom mode)
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
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f));  // 0-100%
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    // Tone control (brightness/darkness)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tone", "Tone",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));

    // Mix control
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wetDryMix", "Wet/Dry Mix", 0.0f, 100.0f, 100.0f));

    return { params.begin(), params.end() };
}

void HarmonicGeneratorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    oversampling.initProcessing(static_cast<size_t>(samplesPerBlock));
    oversampling.reset();
    lastSampleRate = sampleRate;

    // Prepare hardware saturation
    hardwareSaturation.prepare(sampleRate);

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
    float inputPeakL = 0.0f, inputPeakR = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        inputPeakL = juce::jmax(inputPeakL, std::abs(buffer.getSample(0, i)));
        if (totalNumOutputChannels > 1)
            inputPeakR = juce::jmax(inputPeakR, std::abs(buffer.getSample(1, i)));
    }

    const float attackTime = 0.3f;   // Fast attack
    const float releaseTime = 0.1f;  // Slow release

    inputLevelL = inputLevelL < inputPeakL ?
        inputLevelL + (inputPeakL - inputLevelL) * attackTime :
        inputLevelL + (inputPeakL - inputLevelL) * releaseTime;
    inputLevelR = inputLevelR < inputPeakR ?
        inputLevelR + (inputPeakR - inputLevelR) * attackTime :
        inputLevelR + (inputPeakR - inputLevelR) * releaseTime;

    dryBuffer.makeCopyOf(buffer);

    // Process with oversampling if enabled
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

    // Note: Drive, output gain, and mix are handled inside processHarmonics in both hardware and custom modes
    // generateHarmonics only performs harmonic generation (no drive/gain/mix processing)

    // Calculate output levels
    float outputPeakL = 0.0f, outputPeakR = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        outputPeakL = juce::jmax(outputPeakL, std::abs(buffer.getSample(0, i)));
        if (totalNumOutputChannels > 1)
            outputPeakR = juce::jmax(outputPeakR, std::abs(buffer.getSample(1, i)));
    }

    outputLevelL = outputLevelL < outputPeakL ?
        outputLevelL + (outputPeakL - outputLevelL) * attackTime :
        outputLevelL + (outputPeakL - outputLevelL) * releaseTime;
    outputLevelR = outputLevelR < outputPeakR ?
        outputLevelR + (outputPeakR - outputLevelR) * attackTime :
        outputLevelR + (outputPeakR - outputLevelR) * releaseTime;
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

    // Get hardware mode
    int mode = hardwareMode ? static_cast<int>(hardwareMode->load()) : 0;

    // Update hardware saturation parameters
    float driveValue = drive ? drive->load() / 100.0f : 0.5f;  // Convert 0-100 to 0-1
    float mixValue = wetDryMix ? wetDryMix->load() / 100.0f : 1.0f;  // Convert 0-100 to 0-1
    float outGain = outputGain ? outputGain->load() : 0.0f;
    float toneValue = tone ? tone->load() / 100.0f : 0.0f;  // Convert -100 to +100 to -1 to +1

    hardwareSaturation.setDrive(driveValue);
    hardwareSaturation.setMix(mixValue);
    hardwareSaturation.setOutput(outGain);
    hardwareSaturation.setTone(toneValue);

    if (mode > 0)
    {
        // Hardware mode - use HardwareSaturation
        // mode-1 because mode 0 is "Custom"
        hardwareSaturation.setMode(static_cast<HardwareSaturation::Mode>(mode - 1));

        for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            leftChannel[sample] = hardwareSaturation.processSample(leftChannel[sample], 0);

            if (rightChannel != nullptr)
                rightChannel[sample] = hardwareSaturation.processSample(rightChannel[sample], 1);
        }
    }
    else
    {
        // Custom mode - use individual harmonic controls (legacy mode)
        float second = secondHarmonic ? secondHarmonic->load() : 0.0f;
        float third = thirdHarmonic ? thirdHarmonic->load() : 0.0f;
        float fourth = fourthHarmonic ? fourthHarmonic->load() : 0.0f;
        float fifth = fifthHarmonic ? fifthHarmonic->load() : 0.0f;

        float evenMix = evenHarmonics ? evenHarmonics->load() : 0.5f;
        float oddMix = oddHarmonics ? oddHarmonics->load() : 0.5f;
        float warmthAmount = warmth ? warmth->load() : 0.5f;
        float brightnessAmount = brightness ? brightness->load() : 0.5f;

        // Apply drive as gain multiplier
        float driveGain = 1.0f + (driveValue * 4.0f);  // Up to 5x gain
        float outputGainLinear = juce::Decibels::decibelsToGain(outGain);

        for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            // Store dry samples
            float dryL = leftChannel[sample];
            float dryR = rightChannel ? rightChannel[sample] : 0.0f;

            // Process left channel
            float inputL = dryL * driveGain;
            float processedL = generateHarmonics(inputL,
                second * evenMix * (1.0f + warmthAmount),
                third * oddMix * (1.0f + brightnessAmount * 0.5f),
                fourth * evenMix * warmthAmount,
                fifth * oddMix * brightnessAmount);

            // Apply output gain and mix
            processedL *= outputGainLinear;
            leftChannel[sample] = processedL * mixValue + dryL * (1.0f - mixValue);

            // Process right channel if stereo
            if (rightChannel != nullptr)
            {
                float inputR = dryR * driveGain;
                float processedR = generateHarmonics(inputR,
                    second * evenMix * (1.0f + warmthAmount),
                    third * oddMix * (1.0f + brightnessAmount * 0.5f),
                    fourth * evenMix * warmthAmount,
                    fifth * oddMix * brightnessAmount);

                // Apply output gain and mix
                processedR *= outputGainLinear;
                rightChannel[sample] = processedR * mixValue + dryR * (1.0f - mixValue);
            }
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
    output += second * 0.5f * x2;

    // 3rd harmonic (odd - presence)
    output += third * 0.3f * x3;

    // 4th harmonic (even - body)
    output += fourth * 0.2f * x4;

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

void HarmonicGeneratorAudioProcessor::setHardwareMode(HardwareSaturation::Mode mode)
{
    // Convert mode to parameter index (+1 because 0 is "Custom")
    int paramValue = static_cast<int>(mode) + 1;
    if (auto* param = apvts.getParameter("hardwareMode"))
    {
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(paramValue)));
    }
}

HardwareSaturation::Mode HarmonicGeneratorAudioProcessor::getHardwareMode() const
{
    int mode = hardwareMode ? static_cast<int>(hardwareMode->load()) : 0;
    if (mode > 0)
        return static_cast<HardwareSaturation::Mode>(mode - 1);
    return HardwareSaturation::Mode::StuderA800;  // Default
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonicGeneratorAudioProcessor();
}
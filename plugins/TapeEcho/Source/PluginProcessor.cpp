#include "PluginProcessor.h"
#include "PluginEditor.h"

// Parameter IDs
const juce::String TapeEchoProcessor::PARAM_MODE = "mode";
const juce::String TapeEchoProcessor::PARAM_REPEAT_RATE = "repeat_rate";
const juce::String TapeEchoProcessor::PARAM_INTENSITY = "intensity";
const juce::String TapeEchoProcessor::PARAM_ECHO_VOLUME = "echo_volume";
const juce::String TapeEchoProcessor::PARAM_REVERB_VOLUME = "reverb_volume";
const juce::String TapeEchoProcessor::PARAM_BASS = "bass";
const juce::String TapeEchoProcessor::PARAM_TREBLE = "treble";
const juce::String TapeEchoProcessor::PARAM_INPUT_VOLUME = "input_volume";
const juce::String TapeEchoProcessor::PARAM_WOW_FLUTTER = "wow_flutter";
const juce::String TapeEchoProcessor::PARAM_TAPE_AGE = "tape_age";
const juce::String TapeEchoProcessor::PARAM_MOTOR_TORQUE = "motor_torque";
const juce::String TapeEchoProcessor::PARAM_STEREO_MODE = "stereo_mode";
const juce::String TapeEchoProcessor::PARAM_LFO_SHAPE = "lfo_shape";
const juce::String TapeEchoProcessor::PARAM_LFO_RATE = "lfo_rate";

TapeEchoProcessor::TapeEchoProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#endif
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    initializeModeConfigs();
}

TapeEchoProcessor::~TapeEchoProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout TapeEchoProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Mode selector (12 positions)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        PARAM_MODE, "Mode", 0, 11, 0));

    // Main controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_REPEAT_RATE, "Repeat Rate",
        juce::NormalisableRange<float>(50.0f, 1000.0f, 1.0f, 0.5f), 300.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_INTENSITY, "Intensity", 0.0f, 100.0f, 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_ECHO_VOLUME, "Echo Volume", 0.0f, 100.0f, 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_REVERB_VOLUME, "Reverb Volume", 0.0f, 100.0f, 30.0f));

    // EQ controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_BASS, "Bass", -12.0f, 12.0f, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_TREBLE, "Treble", -12.0f, 12.0f, 0.0f));

    // Input control
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_INPUT_VOLUME, "Input Volume", 0.0f, 100.0f, 50.0f));

    // Extended parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_WOW_FLUTTER, "Wow & Flutter", 0.0f, 100.0f, 20.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_TAPE_AGE, "Tape Age", 0.0f, 100.0f, 30.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_MOTOR_TORQUE, "Motor Torque", 0.0f, 100.0f, 80.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        PARAM_STEREO_MODE, "Stereo Mode", false));

    // LFO parameters (internal - not exposed to user)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        PARAM_LFO_SHAPE, "LFO Shape", 0, 5, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        PARAM_LFO_RATE, "LFO Rate", 0.1f, 10.0f, 1.0f));

    return { params.begin(), params.end() };
}

void TapeEchoProcessor::initializeModeConfigs()
{
    // Mode 1-3: Single head echoes
    modeConfigs[0] = {true, false, false, false, {70.0f, 150.0f, 300.0f}};
    modeConfigs[1] = {false, true, false, false, {70.0f, 150.0f, 300.0f}};
    modeConfigs[2] = {false, false, true, false, {70.0f, 150.0f, 300.0f}};

    // Mode 4-6: Two-head combinations
    modeConfigs[3] = {true, true, false, false, {70.0f, 150.0f, 300.0f}};
    modeConfigs[4] = {true, false, true, false, {70.0f, 150.0f, 300.0f}};
    modeConfigs[5] = {false, true, true, false, {70.0f, 150.0f, 300.0f}};

    // Mode 7: All three heads
    modeConfigs[6] = {true, true, true, false, {70.0f, 150.0f, 300.0f}};

    // Mode 8-11: With reverb
    modeConfigs[7] = {true, true, false, true, {70.0f, 150.0f, 300.0f}};
    modeConfigs[8] = {true, false, true, true, {70.0f, 150.0f, 300.0f}};
    modeConfigs[9] = {false, true, true, true, {70.0f, 150.0f, 300.0f}};
    modeConfigs[10] = {true, true, true, true, {70.0f, 150.0f, 300.0f}};

    // Mode 12: Reverb only
    modeConfigs[11] = {false, false, false, true, {70.0f, 150.0f, 300.0f}};
}

const juce::String TapeEchoProcessor::getName() const
{
    return "Vintage Tape Echo";
}

bool TapeEchoProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool TapeEchoProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool TapeEchoProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double TapeEchoProcessor::getTailLengthSeconds() const
{
    return 5.0;
}

int TapeEchoProcessor::getNumPrograms()
{
    return static_cast<int>(getFactoryPresets().size());
}

int TapeEchoProcessor::getCurrentProgram()
{
    return 0;
}

void TapeEchoProcessor::setCurrentProgram(int index)
{
    if (index >= 0 && index < getFactoryPresets().size())
    {
        loadPreset(getFactoryPresets()[index]);
    }
}

const juce::String TapeEchoProcessor::getProgramName(int index)
{
    if (index >= 0 && index < getFactoryPresets().size())
    {
        return getFactoryPresets()[index].name;
    }
    return {};
}

void TapeEchoProcessor::changeProgramName(int index, const juce::String& newName)
{
}

void TapeEchoProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    tapeDelay.prepare(sampleRate, samplesPerBlock);
    springReverb.prepare(sampleRate, samplesPerBlock);
    preamp.prepare(sampleRate, samplesPerBlock);

    // Initialize EQ filters
    bassFilterL.setCoefficients(juce::IIRCoefficients::makeLowShelf(sampleRate, 100.0, 0.7, 1.0));
    bassFilterR.setCoefficients(juce::IIRCoefficients::makeLowShelf(sampleRate, 100.0, 0.7, 1.0));
    trebleFilterL.setCoefficients(juce::IIRCoefficients::makeHighShelf(sampleRate, 3000.0, 0.7, 1.0));
    trebleFilterR.setCoefficients(juce::IIRCoefficients::makeHighShelf(sampleRate, 3000.0, 0.7, 1.0));

    updateDelayConfiguration();
    updateEQFilters();
}

void TapeEchoProcessor::releaseResources()
{
    tapeDelay.reset();
    springReverb.reset();
    preamp.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TapeEchoProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void TapeEchoProcessor::updateDelayConfiguration()
{
    int mode = apvts.getRawParameterValue(PARAM_MODE)->load();
    float repeatRate = apvts.getRawParameterValue(PARAM_REPEAT_RATE)->load();

    if (mode >= 0 && mode < NumModes)
    {
        const ModeConfig& config = modeConfigs[mode];

        // Calculate delay times based on repeat rate
        float rateFactor = repeatRate / 300.0f; // Normalized to default rate

        for (int i = 0; i < 3; ++i)
        {
            tapeDelay.setHeadEnabled(i, i == 0 ? config.head1 : (i == 1 ? config.head2 : config.head3));
            tapeDelay.setDelayTime(i, config.delayTimes[i] * rateFactor);
        }
    }

    // Update other delay parameters
    float intensity = apvts.getRawParameterValue(PARAM_INTENSITY)->load() / 100.0f;
    tapeDelay.setFeedback(intensity * 0.95f);

    // Motor torque affects wow and flutter: lower torque = more pitch instability
    float wowFlutter = apvts.getRawParameterValue(PARAM_WOW_FLUTTER)->load() / 100.0f;
    float motorTorque = apvts.getRawParameterValue(PARAM_MOTOR_TORQUE)->load() / 100.0f;

    // Map motor torque inversely: 0% torque = 2x flutter, 100% torque = 1x flutter
    float torqueMultiplier = 1.0f + (1.0f - motorTorque);
    float effectiveWowFlutter = wowFlutter * torqueMultiplier;

    float lfoRate = apvts.getRawParameterValue(PARAM_LFO_RATE)->load();
    int lfoShape = apvts.getRawParameterValue(PARAM_LFO_SHAPE)->load();
    tapeDelay.setWowFlutter(effectiveWowFlutter, lfoRate, lfoShape);

    float tapeAge = apvts.getRawParameterValue(PARAM_TAPE_AGE)->load() / 100.0f;
    tapeDelay.setTapeAge(tapeAge);
}

void TapeEchoProcessor::updateEQFilters()
{
    double sampleRate = getSampleRate();
    if (sampleRate > 0)
    {
        float bassGain = apvts.getRawParameterValue(PARAM_BASS)->load();
        float trebleGain = apvts.getRawParameterValue(PARAM_TREBLE)->load();

        float bassLinearGain = std::pow(10.0f, bassGain / 20.0f);
        float trebleLinearGain = std::pow(10.0f, trebleGain / 20.0f);

        bassFilterL.setCoefficients(juce::IIRCoefficients::makeLowShelf(sampleRate, 100.0, 0.7, bassLinearGain));
        bassFilterR.setCoefficients(juce::IIRCoefficients::makeLowShelf(sampleRate, 100.0, 0.7, bassLinearGain));
        trebleFilterL.setCoefficients(juce::IIRCoefficients::makeHighShelf(sampleRate, 3000.0, 0.7, trebleLinearGain));
        trebleFilterR.setCoefficients(juce::IIRCoefficients::makeHighShelf(sampleRate, 3000.0, 0.7, trebleLinearGain));
    }
}

void TapeEchoProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    updateDelayConfiguration();
    updateEQFilters();

    float inputVolume = apvts.getRawParameterValue(PARAM_INPUT_VOLUME)->load() / 50.0f;  // 50% = 1.0 (unity gain)
    float echoVolume = apvts.getRawParameterValue(PARAM_ECHO_VOLUME)->load() / 100.0f;
    float reverbVolume = apvts.getRawParameterValue(PARAM_REVERB_VOLUME)->load() / 100.0f;

    int mode = apvts.getRawParameterValue(PARAM_MODE)->load();
    bool reverbEnabled = modeConfigs[mode].reverb;
    bool stereoMode = apvts.getRawParameterValue(PARAM_STEREO_MODE)->load();

    preamp.setInputGain(inputVolume);

    float peakLevel = 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            float* channelData = buffer.getWritePointer(channel);
            float input = channelData[sample];

            // Apply preamp saturation
            float processed = preamp.processSample(input);

            // Get the previous delay output and apply EQ to it for feedback
            // This is the key change: EQ is now in the feedback path
            float filteredFeedback = 0.0f;
            if (channel == 0)
            {
                filteredFeedback = bassFilterL.processSingleSampleRaw(lastDelayOutputL);
                filteredFeedback = trebleFilterL.processSingleSampleRaw(filteredFeedback);
            }
            else
            {
                filteredFeedback = bassFilterR.processSingleSampleRaw(lastDelayOutputR);
                filteredFeedback = trebleFilterR.processSingleSampleRaw(filteredFeedback);
            }

            // Process through tape delay with the filtered feedback
            float delayed = tapeDelay.processSample(processed, filteredFeedback, channel);

            // Store the raw delay output for next sample's feedback
            if (channel == 0)
                lastDelayOutputL = delayed;
            else
                lastDelayOutputR = delayed;

            // Process through spring reverb if enabled
            float reverbed = 0.0f;
            if (reverbEnabled)
            {
                reverbed = springReverb.processSample(processed + delayed * 0.3f, channel);
            }

            // Mix signals
            float output = input + delayed * echoVolume + reverbed * reverbVolume;

            // Apply stereo spreading if enabled
            if (stereoMode && totalNumInputChannels == 2)
            {
                if (channel == 0)
                {
                    output = output * 0.8f + delayed * echoVolume * 0.2f;
                }
                else
                {
                    output = output * 0.8f - delayed * echoVolume * 0.2f;
                }
            }

            // Soft clipping to prevent harsh distortion
            output = std::tanh(output * 0.7f) / 0.7f;

            channelData[sample] = output;

            // Update peak level for VU meter
            peakLevel = std::max(peakLevel, std::abs(output));
        }
    }

    // Update peak level with decay
    float currentPeak = currentPeakLevel.load();
    currentPeak = std::max(peakLevel, currentPeak * peakDecay);
    currentPeakLevel.store(currentPeak);
}

bool TapeEchoProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* TapeEchoProcessor::createEditor()
{
    return new TapeEchoEditor(*this);
}

void TapeEchoProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void TapeEchoProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

const std::vector<TapeEchoProcessor::Preset>& TapeEchoProcessor::getFactoryPresets()
{
    static const std::vector<Preset> presets = {
        {"Subtle Echo", 250.0f, 30.0f, 40.0f, 0.0f, 0.0f, 0.0f, 50.0f, 10.0f, 20.0f, Mode1_ShortEcho},
        {"Vintage Slapback", 100.0f, 25.0f, 60.0f, 0.0f, -2.0f, -3.0f, 60.0f, 25.0f, 40.0f, Mode1_ShortEcho},
        {"Classic Delay", 350.0f, 45.0f, 50.0f, 0.0f, 0.0f, 0.0f, 50.0f, 15.0f, 25.0f, Mode2_MediumEcho},
        {"Long Echo", 500.0f, 40.0f, 45.0f, 0.0f, -1.0f, -2.0f, 50.0f, 20.0f, 30.0f, Mode3_LongEcho},
        {"Rhythmic Pattern", 300.0f, 50.0f, 55.0f, 0.0f, 0.0f, 0.0f, 50.0f, 10.0f, 15.0f, Mode4_ShortMedium},
        {"Cascading Echoes", 400.0f, 60.0f, 50.0f, 0.0f, -1.0f, -1.0f, 55.0f, 15.0f, 25.0f, Mode7_AllHeads},
        {"Echo Chamber", 350.0f, 55.0f, 45.0f, 35.0f, 0.0f, -2.0f, 50.0f, 20.0f, 35.0f, Mode8_ShortMediumReverb},
        {"Dreamy Space", 450.0f, 50.0f, 40.0f, 50.0f, 0.0f, -3.0f, 50.0f, 25.0f, 40.0f, Mode10_MediumLongReverb},
        {"Ambient Wash", 600.0f, 65.0f, 35.0f, 60.0f, -2.0f, -4.0f, 45.0f, 30.0f, 45.0f, Mode11_AllHeadsReverb},
        {"Spring Reverb", 0.0f, 0.0f, 0.0f, 70.0f, 0.0f, 0.0f, 50.0f, 0.0f, 0.0f, Mode12_ReverbOnly},
        {"Dub Echo", 375.0f, 75.0f, 60.0f, 20.0f, 3.0f, -5.0f, 65.0f, 35.0f, 50.0f, Mode7_AllHeads},
        {"Self-Oscillation", 300.0f, 95.0f, 70.0f, 0.0f, 0.0f, 0.0f, 70.0f, 40.0f, 20.0f, Mode7_AllHeads},
        {"Rockabilly Slap", 85.0f, 20.0f, 65.0f, 10.0f, 2.0f, 3.0f, 60.0f, 20.0f, 35.0f, Mode1_ShortEcho},
        {"Psychedelic", 425.0f, 70.0f, 55.0f, 45.0f, -3.0f, 2.0f, 55.0f, 45.0f, 60.0f, Mode11_AllHeadsReverb},
        {"Clean Digital", 300.0f, 40.0f, 50.0f, 0.0f, 0.0f, 0.0f, 50.0f, 5.0f, 5.0f, Mode2_MediumEcho},
        {"Worn Tape", 320.0f, 45.0f, 48.0f, 15.0f, -4.0f, -6.0f, 52.0f, 60.0f, 80.0f, Mode5_ShortLong},
        {"Radio Echo", 280.0f, 35.0f, 55.0f, 8.0f, -2.0f, 4.0f, 48.0f, 25.0f, 40.0f, Mode4_ShortMedium},
        {"Cathedral", 550.0f, 60.0f, 30.0f, 80.0f, -3.0f, -4.0f, 45.0f, 15.0f, 25.0f, Mode12_ReverbOnly},
        {"Ping Pong", 333.0f, 50.0f, 60.0f, 0.0f, 0.0f, 0.0f, 50.0f, 8.0f, 15.0f, Mode5_ShortLong},
        {"Vintage Reggae", 375.0f, 65.0f, 58.0f, 12.0f, 4.0f, -4.0f, 62.0f, 28.0f, 45.0f, Mode6_MediumLong},
        {"Space Station", 666.0f, 80.0f, 42.0f, 65.0f, -5.0f, 3.0f, 48.0f, 50.0f, 70.0f, Mode11_AllHeadsReverb},
        {"Tape Flanger", 45.0f, 85.0f, 75.0f, 0.0f, 0.0f, 0.0f, 65.0f, 70.0f, 30.0f, Mode1_ShortEcho},
        {"Vocal Double", 35.0f, 15.0f, 45.0f, 5.0f, 0.0f, 2.0f, 50.0f, 12.0f, 20.0f, Mode1_ShortEcho},
        {"Lead Guitar", 320.0f, 38.0f, 52.0f, 18.0f, 1.0f, 3.0f, 55.0f, 18.0f, 28.0f, Mode4_ShortMedium},
        {"Drum Room", 125.0f, 22.0f, 48.0f, 35.0f, -1.0f, -2.0f, 58.0f, 15.0f, 25.0f, Mode8_ShortMediumReverb},
        {"Broken Machine", 285.0f, 88.0f, 62.0f, 8.0f, -6.0f, -8.0f, 68.0f, 85.0f, 95.0f, Mode7_AllHeads},
        {"Surf Guitar", 315.0f, 42.0f, 58.0f, 25.0f, 2.0f, 4.0f, 52.0f, 22.0f, 35.0f, Mode9_ShortLongReverb},
        {"Experimental", 777.0f, 92.0f, 38.0f, 55.0f, -8.0f, 8.0f, 72.0f, 75.0f, 85.0f, Mode11_AllHeadsReverb},
        {"50s Echo", 280.0f, 35.0f, 55.0f, 15.0f, -3.0f, -5.0f, 48.0f, 35.0f, 55.0f, Mode2_MediumEcho},
        {"Modern Production", 250.0f, 30.0f, 40.0f, 20.0f, 0.0f, 1.0f, 50.0f, 8.0f, 10.0f, Mode4_ShortMedium},
        {"Retro Sci-Fi", 444.0f, 72.0f, 52.0f, 42.0f, -4.0f, 6.0f, 56.0f, 55.0f, 65.0f, Mode10_MediumLongReverb},
        {"Jazz Club", 185.0f, 28.0f, 42.0f, 45.0f, -2.0f, -3.0f, 45.0f, 20.0f, 35.0f, Mode12_ReverbOnly},
        {"Haunted House", 666.0f, 78.0f, 35.0f, 72.0f, -6.0f, -7.0f, 42.0f, 65.0f, 80.0f, Mode11_AllHeadsReverb},
        {"Nashville Sound", 265.0f, 32.0f, 48.0f, 8.0f, 1.0f, 2.0f, 52.0f, 15.0f, 25.0f, Mode2_MediumEcho},
        {"Berlin School", 375.0f, 55.0f, 50.0f, 30.0f, -2.0f, 0.0f, 50.0f, 25.0f, 35.0f, Mode7_AllHeads},
        {"Shoegaze", 425.0f, 68.0f, 45.0f, 55.0f, -4.0f, -2.0f, 48.0f, 45.0f, 55.0f, Mode11_AllHeadsReverb},
        {"Vintage Broadcast", 225.0f, 25.0f, 52.0f, 5.0f, -1.0f, 5.0f, 48.0f, 30.0f, 50.0f, Mode1_ShortEcho},
        {"Post-Rock", 525.0f, 62.0f, 48.0f, 38.0f, -3.0f, -1.0f, 52.0f, 35.0f, 45.0f, Mode10_MediumLongReverb},
        {"Garage Rock", 195.0f, 45.0f, 62.0f, 12.0f, 3.0f, 4.0f, 65.0f, 40.0f, 60.0f, Mode4_ShortMedium},
        {"Ethereal", 485.0f, 58.0f, 38.0f, 65.0f, -5.0f, -3.0f, 42.0f, 30.0f, 40.0f, Mode11_AllHeadsReverb},
        {"Memphis Soul", 295.0f, 38.0f, 52.0f, 22.0f, 2.0f, -1.0f, 55.0f, 25.0f, 40.0f, Mode5_ShortLong},
        {"Detroit Techno", 333.0f, 52.0f, 58.0f, 5.0f, -2.0f, 3.0f, 60.0f, 12.0f, 18.0f, Mode6_MediumLong},
        {"Film Noir", 385.0f, 48.0f, 42.0f, 48.0f, -4.0f, -5.0f, 45.0f, 35.0f, 55.0f, Mode9_ShortLongReverb},
        {"New Wave", 315.0f, 42.0f, 55.0f, 15.0f, 0.0f, 4.0f, 52.0f, 20.0f, 30.0f, Mode4_ShortMedium},
        {"Vintage Disco", 285.0f, 35.0f, 50.0f, 25.0f, 3.0f, 2.0f, 55.0f, 18.0f, 28.0f, Mode5_ShortLong},
        {"Alternative Rock", 365.0f, 48.0f, 52.0f, 18.0f, 1.0f, 1.0f, 58.0f, 22.0f, 32.0f, Mode6_MediumLong},
        {"Dream Pop", 445.0f, 55.0f, 40.0f, 52.0f, -3.0f, -2.0f, 45.0f, 35.0f, 45.0f, Mode10_MediumLongReverb},
        {"Classic Country", 245.0f, 30.0f, 48.0f, 10.0f, 2.0f, 1.0f, 52.0f, 20.0f, 35.0f, Mode2_MediumEcho},
        {"Industrial", 395.0f, 75.0f, 60.0f, 8.0f, -5.0f, 5.0f, 68.0f, 55.0f, 70.0f, Mode7_AllHeads},
        {"Lo-Fi Hip Hop", 325.0f, 40.0f, 45.0f, 20.0f, -3.0f, -4.0f, 48.0f, 45.0f, 65.0f, Mode5_ShortLong}
    };
    return presets;
}

void TapeEchoProcessor::loadPreset(const Preset& preset)
{
    apvts.getParameter(PARAM_MODE)->setValueNotifyingHost(preset.mode / 11.0f);
    apvts.getParameter(PARAM_REPEAT_RATE)->setValueNotifyingHost((preset.repeatRate - 50.0f) / 950.0f);
    apvts.getParameter(PARAM_INTENSITY)->setValueNotifyingHost(preset.intensity / 100.0f);
    apvts.getParameter(PARAM_ECHO_VOLUME)->setValueNotifyingHost(preset.echoVolume / 100.0f);
    apvts.getParameter(PARAM_REVERB_VOLUME)->setValueNotifyingHost(preset.reverbVolume / 100.0f);
    apvts.getParameter(PARAM_BASS)->setValueNotifyingHost((preset.bass + 12.0f) / 24.0f);
    apvts.getParameter(PARAM_TREBLE)->setValueNotifyingHost((preset.treble + 12.0f) / 24.0f);
    apvts.getParameter(PARAM_INPUT_VOLUME)->setValueNotifyingHost(preset.inputVolume / 100.0f);
    apvts.getParameter(PARAM_WOW_FLUTTER)->setValueNotifyingHost(preset.wowFlutter / 100.0f);
    apvts.getParameter(PARAM_TAPE_AGE)->setValueNotifyingHost(preset.tapeAge / 100.0f);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeEchoProcessor();
}
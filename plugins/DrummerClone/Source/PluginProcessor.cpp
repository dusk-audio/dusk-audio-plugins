#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Helper function to create parameter layout
static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Core parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("complexity", 1), "Complexity",
        juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f), 5.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("loudness", 1), "Loudness",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 75.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("swing", 1), "Swing",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f));

    // Follow Mode parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("followEnabled", 1), "Follow Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("followSource", 1), "Follow Source",
        juce::StringArray{"MIDI", "Audio"}, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("followSensitivity", 1), "Follow Sensitivity",
        juce::NormalisableRange<float>(0.1f, 0.8f, 0.01f), 0.5f));

    // Style parameters
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("style", 1), "Style",
        juce::StringArray{"Rock", "HipHop", "Alternative", "R&B", "Electronic", "Trap", "Songwriter"}, 0));

    // Build drummer list to match DrummerDNA profiles order in createDefaultProfiles()
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("drummer", 1), "Drummer",
        juce::StringArray{
            // Original drummers (indices 0-11)
            "Kyle - Rock", "Anders - Rock", "Max - Rock",           // Rock (0-2)
            "Logan - Alternative", "Aidan - Alternative",           // Alternative (3-4)
            "Austin - HipHop", "Tyrell - HipHop",                   // HipHop (5-6)
            "Brooklyn - R&B", "Darnell - R&B",                      // R&B (7-8)
            "Niklas - Electronic", "Lexi - Electronic",             // Electronic (9-10)
            "Jesse - Songwriter", "Maya - Songwriter",              // Songwriter (11-12)
            // New drummers (indices 13-27)
            "Emily - Songwriter", "Sam - Songwriter",               // Songwriter (13-14)
            "Xavier - Trap", "Jayden - Trap", "Zion - Trap", "Luna - Trap",  // Trap (15-18)
            "Ricky - Rock", "Jake - Rock",                          // Additional Rock (19-20)
            "River - Alternative", "Quinn - Alternative",           // Additional Alternative (21-22)
            "Marcus - HipHop", "Kira - HipHop",                     // Additional HipHop (23-24)
            "Aaliyah - R&B", "Andre - R&B",                         // Additional R&B (25-26)
            "Sasha - Electronic", "Felix - Electronic"              // Additional Electronic (27-28)
        }, 0));

    // Fill parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("fillFrequency", 1), "Fill Frequency",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 30.0f));  // How often fills occur (%)

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("fillIntensity", 1), "Fill Intensity",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));  // How intense fills are

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("fillLength", 1), "Fill Length",
        juce::StringArray{"1 Beat", "2 Beats", "4 Beats"}, 0));  // Length of fills

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("fillTrigger", 1), "Trigger Fill", false));  // Manual fill trigger

    // Section arrangement parameter
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("section", 1), "Section",
        juce::StringArray{"Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Breakdown", "Outro"}, 1));  // Default to Verse

    // Advanced Humanization parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("humanTiming", 1), "Timing Variation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 20.0f));  // Timing randomization %

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("humanVelocity", 1), "Velocity Variation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 15.0f));  // Velocity randomization %

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("humanPush", 1), "Push/Drag Feel",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 1.0f), 0.0f));  // Ahead/behind the beat

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("humanGroove", 1), "Groove Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));  // How much groove template applies

    // MIDI CC Control parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("midiCCEnabled", 1), "MIDI CC Control", true));  // Enable MIDI CC for section/fill

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID("sectionCC", 1), "Section CC#",
        1, 127, 102));  // CC number for section control (default CC 102)

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID("fillTriggerCC", 1), "Fill Trigger CC#",
        1, 127, 103));  // CC number for fill trigger (default CC 103)

    // Engine mode parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("usePatternLibrary", 1), "Use Pattern Library", true));  // Enable pattern-based generation

    // Kit piece enable/disable parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("kitKick", 1), "Kick Enabled", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("kitSnare", 1), "Snare Enabled", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("kitHiHat", 1), "Hi-Hat Enabled", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("kitToms", 1), "Toms Enabled", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("kitCymbals", 1), "Cymbals Enabled", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("kitPercussion", 1), "Percussion Enabled", true));

    return { params.begin(), params.end() };
}

//==============================================================================
DrummerCloneAudioProcessor::DrummerCloneAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),  // Sidechain for audio Follow Mode (no audio output - MIDI only)
       parameters(*this, nullptr, juce::Identifier("DrummerCloneParameters"), createParameterLayout()),
       audioInputBuffer(2, 44100 * 2),  // 2 seconds stereo buffer at 44.1kHz
       drummerEngine(parameters)
{
    // Set up parameter listeners
    parameters.addParameterListener(PARAM_COMPLEXITY, this);
    parameters.addParameterListener(PARAM_LOUDNESS, this);
    parameters.addParameterListener(PARAM_SWING, this);
    parameters.addParameterListener(PARAM_FOLLOW_ENABLED, this);
    parameters.addParameterListener(PARAM_FOLLOW_SOURCE, this);
    parameters.addParameterListener(PARAM_FOLLOW_SENSITIVITY, this);
    parameters.addParameterListener(PARAM_STYLE, this);
    parameters.addParameterListener(PARAM_DRUMMER, this);

    // Start timer for UI updates (100ms)
    startTimer(100);
}

DrummerCloneAudioProcessor::~DrummerCloneAudioProcessor()
{
    stopTimer();
}

//==============================================================================
const juce::String DrummerCloneAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DrummerCloneAudioProcessor::acceptsMidi() const
{
    return true;
}

bool DrummerCloneAudioProcessor::producesMidi() const
{
    return true;
}

bool DrummerCloneAudioProcessor::isMidiEffect() const
{
    // Return false to enable sidechain audio input for Follow Mode
    return false;
}

double DrummerCloneAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DrummerCloneAudioProcessor::getNumPrograms()
{
    return 1;
}

int DrummerCloneAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DrummerCloneAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String DrummerCloneAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void DrummerCloneAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void DrummerCloneAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentSamplesPerBlock = samplesPerBlock;

    // Prepare buffers (stereo for sidechain input)
    audioInputBuffer.setSize(2, static_cast<int>(sampleRate * 2)); // 2 second stereo buffer
    audioInputBuffer.clear();

    // Prepare Follow Mode components
    transientDetector.prepare(sampleRate);
    midiGrooveExtractor.prepare(sampleRate);
    grooveTemplateGenerator.prepare(sampleRate);
    grooveLearner.prepare(sampleRate, currentBPM);

    // Prepare drum engine
    drummerEngine.prepare(sampleRate, samplesPerBlock);
}

void DrummerCloneAudioProcessor::releaseResources()
{
    // Clean up resources
}

bool DrummerCloneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // This is a MIDI-only plugin - we accept sidechain input for Follow Mode
    // but don't produce audio output (only MIDI)
    auto inputSet = layouts.getMainInputChannelSet();
    auto outputSet = layouts.getMainOutputChannelSet();

    // Allow stereo/mono/disabled input (sidechain is optional for Follow Mode)
    if (inputSet != juce::AudioChannelSet::stereo() &&
        inputSet != juce::AudioChannelSet::mono() &&
        !inputSet.isDisabled())
        return false;

    // Output should be disabled or minimal (we're MIDI-only)
    // But some DAWs require at least a mono output for plugin to work
    if (outputSet != juce::AudioChannelSet::stereo() &&
        outputSet != juce::AudioChannelSet::mono() &&
        !outputSet.isDisabled())
        return false;

    return true;
}

void DrummerCloneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Update MIDI section indicator decay (show "MIDI" for 2 seconds after last CC)
    double blockDuration = static_cast<double>(buffer.getNumSamples()) / currentSampleRate;
    timeSinceLastMidiSection += blockDuration;
    if (timeSinceLastMidiSection > 2.0)
    {
        midiSectionActive = false;
    }

    // Get playhead information
    updatePlayheadInfo(getPlayHead());

    // Process incoming MIDI for CC control and Follow Mode
    processMidiInput(midiMessages);

    // Process Follow Mode if enabled
    if (followModeActive)
    {
        // Get the sidechain input bus (bus index 0 is the first input bus)
        // The sidechain audio is used for Follow Mode transient detection
        auto sidechainInput = getBusBuffer(buffer, true, 0);  // true = input bus, 0 = first input bus

        if (sidechainInput.getNumChannels() > 0 && sidechainInput.getNumSamples() > 0)
        {
            processFollowMode(sidechainInput, midiMessages);
        }
    }

    // Clear input MIDI - we generate our own
    midiMessages.clear();

    // Only generate and output MIDI when DAW is playing
    if (isPlaying)
    {
        // Calculate bar length
        double barLengthPpq = timeSignatureNumerator * (4.0 / timeSignatureDenominator);
        int currentBar = static_cast<int>(std::floor(ppqPosition / barLengthPpq));

        // Generate drum pattern if needed (at bar boundaries or first time)
        if (needsRegeneration.load() || currentBar != lastGeneratedBar)
        {
            generateDrumPattern();
            needsRegeneration.store(false);
        }

        // Convert generated MIDI from PPQ ticks to sample positions for this buffer
        if (!generatedMidiBuffer.isEmpty())
        {
            // Calculate timing values
            double samplesPerBeat = (currentSampleRate * 60.0) / currentBPM;
            double bufferStartPpq = ppqPosition;
            double bufferDurationPpq = buffer.getNumSamples() / samplesPerBeat;
            double bufferEndPpq = bufferStartPpq + bufferDurationPpq;

            // Current bar start in PPQ
            double currentBarStartPpq = currentBar * barLengthPpq;

            // Ticks per quarter note in the generated buffer
            const double ticksPerQuarter = 960.0;  // DrummerEngine::PPQ

            for (const auto metadata : generatedMidiBuffer)
            {
                auto msg = metadata.getMessage();

                // Convert tick timestamp to PPQ position relative to bar start
                double eventTickInBar = msg.getTimeStamp();
                double eventPpqInBar = eventTickInBar / ticksPerQuarter;
                double eventAbsolutePpq = currentBarStartPpq + eventPpqInBar;

                // Check if event falls within this buffer's time range
                if (eventAbsolutePpq >= bufferStartPpq && eventAbsolutePpq < bufferEndPpq)
                {
                    // Convert to sample position within buffer
                    double ppqOffset = eventAbsolutePpq - bufferStartPpq;
                    int samplePosition = static_cast<int>(ppqOffset * samplesPerBeat);
                    samplePosition = juce::jlimit(0, buffer.getNumSamples() - 1, samplePosition);

                    midiMessages.addEvent(msg, samplePosition);
                }
            }
        }
    }
    else
    {
        // Not playing - reset state
        generatedMidiBuffer.clear();
        lastGeneratedBar = -1;  // Reset so we regenerate when play starts
    }

    // Clear output audio - we're a MIDI-only plugin
    // Audio input is only used for Follow Mode analysis, we don't pass it through
    buffer.clear();
}

void DrummerCloneAudioProcessor::updatePlayheadInfo(juce::AudioPlayHead* head)
{
    if (head != nullptr)
    {
        if (auto position = head->getPosition())
        {
            // Get tempo from DAW
            if (position->getBpm().hasValue())
                currentBPM = *position->getBpm();

            // Get position from DAW
            if (position->getPpqPosition().hasValue())
                ppqPosition = *position->getPpqPosition();

            // Get time signature from DAW
            if (position->getTimeSignature().hasValue())
            {
                auto timeSig = *position->getTimeSignature();
                timeSignatureNumerator = timeSig.numerator;
                timeSignatureDenominator = timeSig.denominator;
            }

            // Get transport state from DAW
            isPlaying = position->getIsPlaying();
        }
    }
    else
    {
        // No playhead - can't generate properly synced MIDI
        isPlaying = false;
    }
}

void DrummerCloneAudioProcessor::processFollowMode(const juce::AudioBuffer<float>& buffer,
                                                   const juce::MidiBuffer& midi)
{
    // Only process follow mode when DAW is playing
    if (!isPlaying)
        return;

    if (followSourceIsAudio)
    {
        // Analyze audio for transients
        auto detectedOnsets = transientDetector.process(buffer);

        // Feed transients to the groove learner (protected by lock)
        const juce::SpinLock::ScopedLockType lock(grooveLearnerLock);

        auto learnerState = grooveLearner.getState();

        // Only process if we're learning or have onsets to analyze
        if (learnerState == GrooveLearner::State::Learning)
        {
            // Update learner with tempo and time signature
            grooveLearner.setBPM(currentBPM);
            grooveLearner.setTimeSignature(timeSignatureNumerator, timeSignatureDenominator);

            // Process onsets through the learner
            grooveLearner.processOnsets(detectedOnsets, ppqPosition, buffer.getNumSamples());

            // Update progress display
            grooveLockPercentage = grooveLearner.getLearningProgress() * 100.0f;

            // Check if learning auto-completed (locked)
            if (grooveLearner.getState() == GrooveLearner::State::Locked)
            {
                currentGroove = grooveLearner.getGrooveTemplate();
                DBG("DrummerClone: Groove learning auto-locked after " << grooveLearner.getBarsLearned() << " bars");
            }
        }
        else if (learnerState == GrooveLearner::State::Locked)
        {
            // Use the locked groove
            currentGroove = grooveLearner.getGrooveTemplate();
            grooveLockPercentage = 100.0f;
        }
        else if (!detectedOnsets.empty())
        {
            // Idle state with onsets - do real-time analysis (no learning)
            currentGroove = grooveTemplateGenerator.generateFromOnsets(
                detectedOnsets, currentBPM, currentSampleRate);
            grooveFollower.update(currentGroove);
            grooveLockPercentage = grooveFollower.getLockPercentage();
        }
    }
    else
    {
        // Analyze MIDI for groove (real-time only for now)
        auto extractedGroove = midiGrooveExtractor.extractFromBuffer(midi);

        if (extractedGroove.noteCount > 0)
        {
            const juce::SpinLock::ScopedLockType lock(grooveLearnerLock);
            currentGroove = grooveTemplateGenerator.generateFromMidi(
                extractedGroove, currentBPM);

            grooveFollower.update(currentGroove);
            grooveLockPercentage = grooveFollower.getLockPercentage();
        }
    }
}

void DrummerCloneAudioProcessor::startGrooveLearning()
{
    const juce::SpinLock::ScopedLockType lock(grooveLearnerLock);
    grooveLearner.startLearning();
    needsRegeneration.store(true);
}

void DrummerCloneAudioProcessor::lockGroove()
{
    const juce::SpinLock::ScopedLockType lock(grooveLearnerLock);
    grooveLearner.lockGroove();
    needsRegeneration.store(true);
}

void DrummerCloneAudioProcessor::resetGrooveLearning()
{
    const juce::SpinLock::ScopedLockType lock(grooveLearnerLock);
    grooveLearner.reset();
    currentGroove.reset();
    grooveLockPercentage = 0.0f;
    needsRegeneration.store(true);
}

void DrummerCloneAudioProcessor::processMidiInput(const juce::MidiBuffer& midiMessages)
{
    // Check if MIDI CC control is enabled
    auto midiCCEnabledParam = parameters.getRawParameterValue("midiCCEnabled");
    bool midiCCEnabled = midiCCEnabledParam ? midiCCEnabledParam->load() > 0.5f : true;

    if (!midiCCEnabled)
    {
        // Still process for Follow Mode ring buffer if needed
        if (followModeActive && !followSourceIsAudio)
        {
            for (const auto metadata : midiMessages)
            {
                if (metadata.getMessage().isNoteOn())
                    midiRingBuffer.push_back(metadata.getMessage());
            }
            pruneOldMidiEvents();
        }
        return;
    }

    // Get CC numbers for section and fill control
    auto sectionCCParam = parameters.getRawParameterValue("sectionCC");
    auto fillCCParam = parameters.getRawParameterValue("fillTriggerCC");
    int sectionCCNumber = sectionCCParam ? static_cast<int>(sectionCCParam->load()) : 102;
    int fillCCNumber = fillCCParam ? static_cast<int>(fillCCParam->load()) : 103;

    for (const auto metadata : midiMessages)
    {
        const auto& message = metadata.getMessage();

        // Handle CC messages
        if (message.isController())
        {
            int ccNumber = message.getControllerNumber();
            int ccValue = message.getControllerValue();

            // Section control via CC
            // CC value 0-127 maps to 7 sections:
            // 0-17 = Intro, 18-35 = Verse, 36-53 = Pre-Chorus, 54-71 = Chorus,
            // 72-89 = Bridge, 90-107 = Breakdown, 108-127 = Outro
            if (ccNumber == sectionCCNumber)
            {
                int sectionIndex = ccValue / 18;  // Map 0-127 to 0-6 (0-17→0, 18-35→1, etc.)
                sectionIndex = juce::jlimit(0, 6, sectionIndex);

                if (auto* param = parameters.getParameter("section"))
                {
                    // Convert to normalized value (0.0 to 1.0)
                    float normalizedValue = static_cast<float>(sectionIndex) / 6.0f;
                    param->setValueNotifyingHost(normalizedValue);
                }

                lastMidiSectionChange = true;
                midiSectionActive = true;
                timeSinceLastMidiSection = 0.0;
                needsRegeneration.store(true);
            }
            // Fill trigger via CC (any value > 64 triggers)
            else if (ccNumber == fillCCNumber && ccValue > 64)
            {
                if (auto* param = parameters.getParameter("fillTrigger"))
                {
                    param->setValueNotifyingHost(1.0f);
                }
            }
        }

        // Store note-ons for Follow Mode
        if (followModeActive && !followSourceIsAudio && message.isNoteOn())
        {
            midiRingBuffer.push_back(message);
        }
    }

    // Prune old MIDI events from ring buffer
    pruneOldMidiEvents();
}

void DrummerCloneAudioProcessor::pruneOldMidiEvents()
{
    auto currentTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    midiRingBuffer.erase(
        std::remove_if(midiRingBuffer.begin(), midiRingBuffer.end(),
            [currentTime](const juce::MidiMessage& m) {
                return (currentTime - m.getTimeStamp()) > 2.0;
            }),
        midiRingBuffer.end());
}

void DrummerCloneAudioProcessor::generateDrumPattern()
{
    // Update engine with current time signature from DAW
    drummerEngine.setTimeSignature(timeSignatureNumerator, timeSignatureDenominator);

    // Update kit piece enable mask from parameters
    DrummerEngine::KitEnableMask kitMask;
    if (auto* p = parameters.getRawParameterValue("kitKick"))
        kitMask.kick = p->load() > 0.5f;
    if (auto* p = parameters.getRawParameterValue("kitSnare"))
        kitMask.snare = p->load() > 0.5f;
    if (auto* p = parameters.getRawParameterValue("kitHiHat"))
        kitMask.hihat = p->load() > 0.5f;
    if (auto* p = parameters.getRawParameterValue("kitToms"))
        kitMask.toms = p->load() > 0.5f;
    if (auto* p = parameters.getRawParameterValue("kitCymbals"))
        kitMask.cymbals = p->load() > 0.5f;
    if (auto* p = parameters.getRawParameterValue("kitPercussion"))
        kitMask.percussion = p->load() > 0.5f;
    drummerEngine.setKitEnableMask(kitMask);

    // Calculate bar length based on time signature
    // PPQ position is in quarter notes
    double barLengthInQuarters = timeSignatureNumerator * (4.0 / timeSignatureDenominator);
    int currentBar = static_cast<int>(ppqPosition / barLengthInQuarters);

    // Only regenerate if we're at a new bar
    if (currentBar != lastGeneratedBar)
    {
        // Get core parameters
        auto complexity = parameters.getRawParameterValue(PARAM_COMPLEXITY)->load();
        auto loudness = parameters.getRawParameterValue(PARAM_LOUDNESS)->load();
        auto swing = parameters.getRawParameterValue(PARAM_SWING)->load();
        auto style = parameters.getParameter(PARAM_STYLE)->getValue();

        // Get section parameter
        auto sectionParam = parameters.getRawParameterValue("section");
        int sectionIndex = sectionParam ? static_cast<int>(sectionParam->load()) : 1;  // Default to Verse
        DrumSection section = static_cast<DrumSection>(sectionIndex);

        // Get humanization parameters
        HumanizeSettings humanize;
        if (auto* p = parameters.getRawParameterValue("humanTiming"))
            humanize.timingVariation = p->load();
        if (auto* p = parameters.getRawParameterValue("humanVelocity"))
            humanize.velocityVariation = p->load();
        if (auto* p = parameters.getRawParameterValue("humanPush"))
            humanize.pushDrag = p->load();
        if (auto* p = parameters.getRawParameterValue("humanGroove"))
            humanize.grooveDepth = p->load();

        // Get fill parameters
        FillSettings fill;
        if (auto* p = parameters.getRawParameterValue("fillFrequency"))
            fill.frequency = p->load();
        if (auto* p = parameters.getRawParameterValue("fillIntensity"))
            fill.intensity = p->load();
        if (auto* p = parameters.getRawParameterValue("fillLength"))
        {
            int lengthIndex = static_cast<int>(p->load());
            fill.lengthBeats = (lengthIndex == 0) ? 1 : (lengthIndex == 1) ? 2 : 4;
        }
        if (auto* p = parameters.getRawParameterValue("fillTrigger"))
        {
            fill.manualTrigger = p->load() > 0.5f;
            // Reset trigger after reading
            if (fill.manualTrigger)
            {
                if (auto* param = parameters.getParameter("fillTrigger"))
                    param->setValueNotifyingHost(0.0f);
            }
        }

        // Apply Follow Mode groove if active
        GrooveTemplate grooveToUse = followModeActive ?
            grooveFollower.getCurrent(ppqPosition) : GrooveTemplate();

        // Check if step sequencer override is active (thread-safe read)
        bool useStepSeq = false;
        std::array<std::array<std::pair<bool, float>, 16>, 8> stepPattern;
        {
            const juce::SpinLock::ScopedLockType lock(stepSeqPatternLock);
            useStepSeq = stepSeqPattern.enabled;
            if (useStepSeq)
            {
                // Convert step sequencer pattern to the format expected by DrummerEngine
                for (int lane = 0; lane < StepSequencerPattern::NumLanes; ++lane)
                {
                    for (int step = 0; step < StepSequencerPattern::NumSteps; ++step)
                    {
                        const auto& stepData = stepSeqPattern.pattern[static_cast<size_t>(lane)][static_cast<size_t>(step)];
                        stepPattern[static_cast<size_t>(lane)][static_cast<size_t>(step)] = {stepData.active, stepData.velocity};
                    }
                }
            }
        }

        if (useStepSeq)
        {
            // Generate from step sequencer
            generatedMidiBuffer = drummerEngine.generateFromStepSequencer(
                stepPattern,
                currentBPM,
                humanize
            );
        }
        else
        {
            // Generate pattern with all parameters (normal mode)
            generatedMidiBuffer = drummerEngine.generateRegion(
                1,  // Generate 1 bar at a time
                currentBPM,
                static_cast<int>(style * 7),  // Convert to style index
                grooveToUse,
                complexity,
                loudness,
                swing,
                section,
                humanize,
                fill
            );
        }

        lastGeneratedBar = currentBar;
    }
}

bool DrummerCloneAudioProcessor::isBarBoundary(double ppq, double bpm)
{
    juce::ignoreUnused(bpm);
    // Use time signature from DAW to determine bar length
    double barLength = timeSignatureNumerator * (4.0 / timeSignatureDenominator);
    int currentBar = static_cast<int>(std::floor(ppq / barLength));

    // Only return true when we've entered a new bar
    if (currentBar != lastGeneratedBar && lastGeneratedBar >= 0)
    {
        return true;
    }

    // First bar after starting playback
    if (lastGeneratedBar < 0)
    {
        return true;
    }

    return false;
}

void DrummerCloneAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == PARAM_FOLLOW_ENABLED)
    {
        followModeActive = newValue > 0.5f;
    }
    else if (parameterID == PARAM_FOLLOW_SOURCE)
    {
        followSourceIsAudio = static_cast<int>(newValue) == 1;
    }
    else if (parameterID == PARAM_FOLLOW_SENSITIVITY)
    {
        followSensitivity = newValue;
        transientDetector.setSensitivity(newValue);
    }
    else if (parameterID == PARAM_DRUMMER)
    {
        // Update the drummer engine when drummer selection changes
        // The parameter is normalized 0-1, so convert to drummer index (0-28)
        int drummerIndex = static_cast<int>(std::round(newValue * 28.0f));
        drummerEngine.setDrummer(drummerIndex);
        needsRegeneration.store(true);
    }
    else if (parameterID == "fillTrigger")
    {
        // Ignore fillTrigger changes - this is a momentary trigger that gets
        // reset programmatically after being read. We don't want the reset
        // to trigger regeneration since the fill is already being processed.
    }
    else
    {
        // Any other parameter change triggers regeneration
        needsRegeneration.store(true);
    }
}

void DrummerCloneAudioProcessor::timerCallback()
{
    // This timer is for UI updates
    // The editor will poll for current state
}

void DrummerCloneAudioProcessor::setStepSequencerPattern(const StepSequencerPattern& pattern)
{
    {
        const juce::SpinLock::ScopedLockType lock(stepSeqPatternLock);
        stepSeqPattern = pattern;
    }
    needsRegeneration.store(true);
}

void DrummerCloneAudioProcessor::setStepSequencerEnabled(bool enabled)
{
    {
        const juce::SpinLock::ScopedLockType lock(stepSeqPatternLock);
        stepSeqPattern.enabled = enabled;
    }
    needsRegeneration.store(true);
}

bool DrummerCloneAudioProcessor::isStepSequencerEnabled() const
{
    const juce::SpinLock::ScopedLockType lock(stepSeqPatternLock);
    return stepSeqPattern.enabled;
}

StepSequencerPattern DrummerCloneAudioProcessor::getStepSequencerPattern() const
{
    const juce::SpinLock::ScopedLockType lock(stepSeqPatternLock);
    return stepSeqPattern;
}

//==============================================================================
bool DrummerCloneAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* DrummerCloneAudioProcessor::createEditor()
{
    return new DrummerCloneAudioProcessorEditor (*this);
}

//==============================================================================
void DrummerCloneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Store parameters
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void DrummerCloneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore parameters
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName(parameters.state.getType()))
        {
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrummerCloneAudioProcessor();
}
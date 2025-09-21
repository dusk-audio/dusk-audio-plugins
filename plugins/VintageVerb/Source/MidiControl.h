/*
  ==============================================================================

    MidiControl.h - Complete MIDI control system for VintageVerb

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <map>
#include <array>

class MidiControlSystem : public juce::MidiInputCallback
{
public:
    MidiControlSystem(juce::AudioProcessorValueTreeState& apvts);
    ~MidiControlSystem();

    // MIDI Setup
    void prepare(double sampleRate);
    void setMidiInput(const juce::String& deviceName);
    void enableMidiLearn(bool enable) { midiLearnEnabled = enable; }

    // CC Mapping
    void mapCCToParameter(int ccNumber, const juce::String& parameterID);
    void removeCCMapping(int ccNumber);
    void clearAllMappings();
    void loadMappingsFromXml(const juce::XmlElement* xml);
    juce::XmlElement* saveMappingsToXml();

    // Program Changes
    void setProgramChangeEnabled(bool enabled) { programChangeEnabled = enabled; }
    void handleProgramChange(int programNumber);

    // Parameter Automation
    void setAutomationRecording(bool recording) { automationRecording = recording; }
    void setAutomationPlayback(bool playback) { automationPlayback = playback; }
    void recordAutomationPoint(const juce::String& paramID, float value, double timestamp);
    void playbackAutomation(double currentTime);

    // MIDI Learn
    bool isMidiLearning() const { return midiLearnEnabled; }
    void setParameterToLearn(const juce::String& paramID);
    juce::String getLastLearnedParameter() const { return lastLearnedParam; }

    // MidiInputCallback
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                  const juce::MidiMessage& message) override;

    // Get current mappings
    std::map<int, juce::String> getCurrentMappings() const { return ccMappings; }

    // MIDI activity monitoring
    bool hasRecentActivity() const { return midiActivityFlag; }
    int getLastCCNumber() const { return lastCCNumber; }
    float getLastCCValue() const { return lastCCValue; }

private:
    juce::AudioProcessorValueTreeState& parameters;

    // MIDI Input
    std::unique_ptr<juce::MidiInput> midiInput;
    juce::String currentMidiDevice;

    // CC Mappings
    std::map<int, juce::String> ccMappings;
    std::map<int, float> ccValues;  // Smoothed CC values
    std::map<int, juce::SmoothedValue<float>> ccSmoothers;

    // Program Change
    bool programChangeEnabled = true;
    int currentProgram = 0;

    // MIDI Learn
    bool midiLearnEnabled = false;
    juce::String parameterToLearn;
    juce::String lastLearnedParam;

    // Automation
    bool automationRecording = false;
    bool automationPlayback = false;

    struct AutomationPoint
    {
        float value;
        double timestamp;
    };

    std::map<juce::String, std::vector<AutomationPoint>> automationData;
    std::map<juce::String, size_t> automationPlayheads;

    // Activity monitoring
    std::atomic<bool> midiActivityFlag{false};
    int lastCCNumber = -1;
    float lastCCValue = 0.0f;
    double sampleRate = 44100.0;

    // Helper functions
    void processCCMessage(int ccNumber, int ccValue);
    void updateParameterFromCC(const juce::String& paramID, float normalizedValue);
    float smoothCCValue(int ccNumber, float targetValue);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiControlSystem)
};

//==============================================================================
// MIDI Modulation Matrix
//==============================================================================
class MidiModulationMatrix
{
public:
    enum class ModSource
    {
        ModWheel,      // CC1
        Breath,        // CC2
        Expression,    // CC11
        AfterTouch,
        PitchBend,
        Velocity,
        CC74,          // Filter cutoff
        CC71,          // Resonance
        CC91,          // Reverb send
        CC93           // Chorus send
    };

    enum class ModDestination
    {
        Mix,
        Size,
        Damping,
        Diffusion,
        PreDelay,
        Width,
        Shimmer,
        Freeze,
        InputGain,
        OutputGain,
        ModulationDepth,
        ModulationRate
    };

    struct ModulationRouting
    {
        ModSource source;
        ModDestination destination;
        float amount;        // -1 to +1
        bool bipolar;       // true for -1 to +1, false for 0 to 1
        float curve;        // 0.1 = exponential, 0.5 = linear, 0.9 = logarithmic
    };

    void addRouting(const ModulationRouting& routing);
    void removeRouting(ModSource source, ModDestination dest);
    void clearAllRoutings();

    float getModulationValue(ModDestination dest) const;
    void updateSourceValue(ModSource source, float value);

    // Preset routings
    void loadFactoryRoutings();
    void saveRoutings(juce::XmlElement* xml);
    void loadRoutings(const juce::XmlElement* xml);

private:
    std::vector<ModulationRouting> routings;
    std::array<float, 10> sourceValues{};  // Current values for each source
    std::array<float, 12> destinationOffsets{};  // Calculated offsets

    float applyModulationCurve(float value, float curve);
};

//==============================================================================
// MIDI Clock Sync
//==============================================================================
class MidiClockSync
{
public:
    MidiClockSync();

    void processTransport(const juce::AudioPlayHead::CurrentPositionInfo& posInfo);
    void handleMidiClock(const juce::MidiMessage& message);

    // Get sync values for time-based effects
    float getSyncedDelayTime(float beatDivision);  // Returns delay in ms
    float getSyncedLFORate(float beatDivision);    // Returns rate in Hz

    bool isPlaying() const { return playing; }
    double getBPM() const { return currentBPM; }
    double getCurrentBar() const { return currentBar; }
    double getCurrentBeat() const { return currentBeat; }

    // Beat divisions
    enum class Division
    {
        Whole = 1,
        Half = 2,
        Quarter = 4,
        Eighth = 8,
        Sixteenth = 16,
        Triplet = 3,
        DottedQuarter = 6,
        DottedEighth = 12
    };

    float getBeatLength(Division div) const;

private:
    bool playing = false;
    double currentBPM = 120.0;
    double currentBar = 0.0;
    double currentBeat = 0.0;
    double ppqPosition = 0.0;

    // MIDI Clock
    int clockTickCount = 0;
    double lastClockTime = 0.0;
    static constexpr int CLOCKS_PER_BEAT = 24;

    // Timing
    juce::Time startTime;
    double sampleRate = 44100.0;
};

//==============================================================================
// MIDI Remote Control Protocol
//==============================================================================
class MidiRemoteControl
{
public:
    // Standard control surfaces support
    enum class Protocol
    {
        Generic,
        HuiProtocol,    // Pro Tools
        MackieControl,  // Logic, Cubase
        AutomapUniversal // Novation
    };

    void setProtocol(Protocol proto) { protocol = proto; }
    void handleControlSurfaceMessage(const juce::MidiMessage& message);
    void sendFeedback(int parameter, float value);

    // Control surface features
    void setMotorizedFader(int channel, float position);
    void setLED(int channel, bool state);
    void setDisplay(int line, const juce::String& text);

private:
    Protocol protocol = Protocol::Generic;

    // Protocol-specific handlers
    void handleHuiMessage(const juce::MidiMessage& message);
    void handleMackieMessage(const juce::MidiMessage& message);
    void handleAutomapMessage(const juce::MidiMessage& message);

    // Feedback
    std::unique_ptr<juce::MidiOutput> midiOutput;
};
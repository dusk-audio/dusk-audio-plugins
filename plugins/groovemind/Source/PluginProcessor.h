/*
  ==============================================================================

    GrooveMind - ML-Powered Intelligent Drummer
    PluginProcessor.h

    A Logic Pro Drummer-inspired MIDI drum pattern generator for Linux.
    Uses machine learning models trained on professional drummer recordings
    to generate contextually appropriate, human-feeling drum patterns.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PatternLibrary.h"
#include "DrummerEngine.h"
#include "GrooveHumanizer.h"
#include "GrooveExtractor.h"

//==============================================================================
class GrooveMindProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    GrooveMindProcessor();
    ~GrooveMindProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter access
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Engine access for editor
    DrummerEngine& getDrummerEngine() { return drummerEngine; }
    PatternLibrary& getPatternLibrary() { return patternLibrary; }
    FollowModeController& getFollowModeController() { return followModeController; }

    // Transport info
    bool isPlaying() const { return transportPlaying; }
    double getCurrentBPM() const { return currentBPM; }
    double getCurrentPositionInBeats() const { return currentPositionBeats; }

    // Follow mode status
    bool isFollowModeEnabled() const;
    bool isFollowModeActive() const;  // Has valid extracted groove

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Core components
    PatternLibrary patternLibrary;
    DrummerEngine drummerEngine;
    GrooveHumanizer grooveHumanizer;
    FollowModeController followModeController;

    // Transport state
    bool transportPlaying = false;
    double currentBPM = 120.0;
    double currentPositionBeats = 0.0;
    int64_t lastSamplePosition = 0;

    // Sample rate
    double sampleRate = 44100.0;

    // MIDI output buffer
    juce::MidiBuffer pendingMidiEvents;

    // Pattern library loading
    void loadPatternLibrary();

    // ML model loading
    void loadMLModels();
    juce::File getResourcesDirectory() const;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMindProcessor)
};

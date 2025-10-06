/*
Tapped delay line, Allpass classes
Delay based on juce::dsp::DelayLine, but allows access to the underlying buffer at specified sample offsets for multiple-tap delays.
*/

#pragma once

#include <JuceHeader.h>
#include "Utilities.h"

template <typename SampleType>
class DelayLineWithSampleAccess
{
public:
    DelayLineWithSampleAccess(int maximumDelayInSamples);
    
    ~DelayLineWithSampleAccess();
    
    void pushSample(int channel, SampleType newValue);
    
    SampleType popSample(int channel);
    
    SampleType getSampleAtDelay(int channel, int delay) const;
    
    void setDelay(int newLength);
    
    void setSize(const int numChannels, const int newSize);
    
    int getNumSamples() const;
    
    void prepare(const juce::dsp::ProcessSpec& spec);
    
    void reset();
private:
    juce::AudioBuffer<SampleType> delayBuffer;
    std::vector<SampleType> v;  // TODO: Unused - consider removing
    int numSamples = 0;
    std::vector<int> writePosition, readPosition;
    SampleType delay = 0.0, delayFrac = 0.0;  // TODO: Unused - for future interpolation?
    int delayInSamples = 0;
    int totalSize = 4;

    double sampleRate = 44100.0;
};

//============================================================================

template <typename SampleType>
class Allpass
{
public:
    Allpass();
    
    ~Allpass();
    
    void setMaximumDelayInSamples(int maxDelayInSamples);
    
    void setDelay(SampleType newDelayInSamples);
    
    void prepare(const juce::dsp::ProcessSpec& spec);
    
    void reset();
    
    void pushSample(int channel, SampleType sample);
    
    SampleType popSample(int channel, SampleType delayInSamples=-1, bool updateReadPointer=true);
    
    void setGain(SampleType newGain);
    
private:
    DelayLineWithSampleAccess<SampleType> delayLine { 44100 };

    int delayInSamples = 4;  // TODO: Appears unused - delay is managed by delayLine
    int numChannels = 1;  // Default to mono; updated in prepare()
    
    SampleType gain = 0.5;
    
    std::vector<SampleType> drySample { };
    std::vector<SampleType> delayOutput { };
    std::vector<SampleType> feedforward { };
    std::vector<SampleType> feedback { };
    
    SampleType sampleRate = 44100.0;
};

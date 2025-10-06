// Tapped delay line, Allpass classes

#include <algorithm>
#include <cmath>
#include "CustomDelays.h"
#include "Utilities.h"

template <typename SampleType>
DelayLineWithSampleAccess<SampleType>::DelayLineWithSampleAccess(int maximumDelayInSamples)
{
    jassert (maximumDelayInSamples >= 0);

    totalSize = maximumDelayInSamples + 1 > 4 ? maximumDelayInSamples + 1 : 4;
    // CRITICAL FIX: delayBuffer has 0 channels at construction, so this creates a 0-channel buffer!
    // Initialize with 1 channel by default, will be resized in prepare()
    delayBuffer.setSize(1, totalSize, false, false, false);
    delayBuffer.clear();
    numSamples = delayBuffer.getNumSamples();
    
}

template <typename SampleType>
DelayLineWithSampleAccess<SampleType>::~DelayLineWithSampleAccess() {}

template <typename SampleType>
void DelayLineWithSampleAccess<SampleType>::pushSample(int channel, SampleType newValue)
{
    jassert(channel >= 0 && channel < static_cast<int>(writePosition.size()));
    const auto ch = static_cast<size_t>(channel);
    delayBuffer.setSample(channel, writePosition[ch], newValue);
    // Optimize wrap-around: use conditional instead of modulo for better performance
    if (++writePosition[ch] >= numSamples)
        writePosition[ch] = 0;
}

template <typename SampleType>
SampleType DelayLineWithSampleAccess<SampleType>::popSample(int channel)
{
    jassert(channel >= 0 && channel < static_cast<int>(readPosition.size()));
    const auto ch = static_cast<size_t>(channel);
    readPosition[ch] = wrapInt((writePosition[ch] - delayInSamples), numSamples);
    return delayBuffer.getSample(channel, readPosition[ch]);
}

template <typename SampleType>
SampleType DelayLineWithSampleAccess<SampleType>::getSampleAtDelay(int channel, int delay) const
{
    jassert(channel >= 0 && channel < static_cast<int>(writePosition.size()));
    const auto ch = static_cast<size_t>(channel);
    return delayBuffer.getSample(channel, wrapInt((writePosition[ch] - delay), numSamples));
}

template <typename SampleType>
void DelayLineWithSampleAccess<SampleType>::setDelay(int newLength)
{
    // Safely clamp to valid range instead of asserting
    delayInSamples = juce::jlimit(0, totalSize, newLength);
}

template <typename SampleType>
void DelayLineWithSampleAccess<SampleType>::setSize(const int numChannels, const int newSize)
{
    totalSize = newSize;
    delayBuffer.setSize(numChannels, totalSize, false, false, true);
    
    reset();
}

template <typename SampleType>
int DelayLineWithSampleAccess<SampleType>::getNumSamples() const { return delayBuffer.getNumSamples(); }

template <typename SampleType>
void DelayLineWithSampleAccess<SampleType>::prepare(const juce::dsp::ProcessSpec& spec)
{
    jassert(spec.numChannels > 0);
    
    delayBuffer.setSize(static_cast<int>(spec.numChannels), totalSize, false, false, true);
    
    writePosition.resize(spec.numChannels);
    readPosition.resize(spec.numChannels);
    
    v.resize(spec.numChannels);
    sampleRate = spec.sampleRate;
    
    reset();
}

template <typename SampleType>
void DelayLineWithSampleAccess<SampleType>::reset()
{
    for (auto vec : {&writePosition, &readPosition})
        std::fill (vec->begin(), vec->end(), 0);
    
    std::fill (v.begin(), v.end(), static_cast<SampleType>(0));
    
    delayBuffer.clear();
}

//============================================================================

template <typename SampleType>
Allpass<SampleType>::Allpass() = default;

template <typename SampleType>
Allpass<SampleType>::~Allpass() = default;

template <typename SampleType>
void Allpass<SampleType>::setMaximumDelayInSamples(int maxDelayInSamples)
{
    // DelayLineWithSampleAccess uses setSize(numChannels, size) instead of setMaximumDelayInSamples
    // Use the actual channel count from prepare() or default to 1 if not yet initialized
    jassert(numChannels > 0);
    delayLine.setSize(numChannels, maxDelayInSamples);
}

template <typename SampleType>
void Allpass<SampleType>::setDelay(SampleType newDelayInSamples)
{
    // DelayLineWithSampleAccess uses setDelay(int) for delay setting
    // Use rounding instead of truncation to minimize timing artifacts
    delayLine.setDelay(static_cast<int>(std::round(newDelayInSamples)));
}

template <typename SampleType>
void Allpass<SampleType>::prepare(const juce::dsp::ProcessSpec& spec)
{
    jassert(spec.numChannels > 0);

    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);  // Store channel count

    delayLine.prepare(spec);
            
    drySample.resize(spec.numChannels);
    delayOutput.resize(spec.numChannels);
    feedforward.resize(spec.numChannels);
    feedback.resize(spec.numChannels);
    
    std::fill(drySample.begin(), drySample.end(), 0.0);
    std::fill(delayOutput.begin(), delayOutput.end(), 0.0);
    std::fill(feedforward.begin(), feedforward.end(), 0.0);
    std::fill(feedback.begin(), feedback.end(), 0.0);
    
    reset();
}

template <typename SampleType>
void Allpass<SampleType>::reset()
{
    delayLine.reset();

    // Also reset the internal state vectors
    std::fill(drySample.begin(), drySample.end(), static_cast<SampleType>(0));
    std::fill(delayOutput.begin(), delayOutput.end(), static_cast<SampleType>(0));
    std::fill(feedforward.begin(), feedforward.end(), static_cast<SampleType>(0));
    std::fill(feedback.begin(), feedback.end(), static_cast<SampleType>(0));
}

template <typename SampleType>
void Allpass<SampleType>::pushSample(int channel, SampleType sample)
{
    jassert(channel >= 0 && channel < static_cast<int>(feedback.size()));
    delayLine.pushSample(channel, sample + feedback[static_cast<size_t>(channel)]);
    drySample[static_cast<size_t>(channel)] = sample;
}

template <typename SampleType>
SampleType Allpass<SampleType>::popSample(int channel, SampleType /*delayInSamples*/, bool /*updateReadPointer*/)
{
    // DelayLineWithSampleAccess popSample only takes channel parameter
    // The delay is already set via setDelay method
    // Note: delayInSamples and updateReadPointer are kept for API compatibility but unused
    jassert(channel >= 0 && channel < static_cast<int>(delayOutput.size()));
    const auto ch = static_cast<size_t>(channel);

    delayOutput[ch] = delayLine.popSample(channel);

    feedback[ch] = delayOutput[ch] * gain;

    feedforward[ch] = -drySample[ch] - delayOutput[ch] * gain;

    return delayOutput[ch] + feedforward[ch];
}

template <typename SampleType>
void Allpass<SampleType>::setGain(SampleType newGain) { gain = std::clamp<SampleType>(newGain, 0.0, 1.0); }

//============================================================================

template class DelayLineWithSampleAccess<float>;
template class DelayLineWithSampleAccess<double>;

template class Allpass<float>;
template class Allpass<double>;

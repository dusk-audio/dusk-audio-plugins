/*
  ==============================================================================

    DattorroPlate.h
    Based on RSAlgorithmicVerb implementation
    Dattorro Plate Reverb - "Effect Design Part 1: Reverberator and Other Filters"
    Jon Dattorro, J. Audio Eng. Soc., 1997

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "CustomDelays.h"

class DattorroPlate
{
public:
    DattorroPlate() = default;
    ~DattorroPlate() = default;

    void prepare(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec monoSpec;
        monoSpec.sampleRate = sampleRate;
        monoSpec.maximumBlockSize = samplesPerBlock;
        monoSpec.numChannels = 1;

        // Prepare all delay lines and allpasses
        allpass1.prepare(monoSpec);
        allpass2.prepare(monoSpec);
        allpass3.prepare(monoSpec);
        allpass4.prepare(monoSpec);
        allpass5.prepare(monoSpec);
        allpass6.prepare(monoSpec);

        decayAPF1.prepare(monoSpec);
        decayAPF2.prepare(monoSpec);

        delay1.prepare(monoSpec);
        delay2.prepare(monoSpec);
        delay3.prepare(monoSpec);
        delay4.prepare(monoSpec);

        inputFilter.prepare(monoSpec);
        dampingFilter1.prepare(monoSpec);
        dampingFilter2.prepare(monoSpec);

        reset();
    }

    void reset()
    {
        allpass1.reset();
        allpass2.reset();
        allpass3.reset();
        allpass4.reset();
        allpass5.reset();
        allpass6.reset();

        decayAPF1.reset();
        decayAPF2.reset();

        delay1.reset();
        delay2.reset();
        delay3.reset();
        delay4.reset();

        inputFilter.reset();
        dampingFilter1.reset();
        dampingFilter2.reset();

        allpassOutput = 0;
        feedback = 0;
        feedforward = 0;
        summingA = 0;
        summingB = 0;
        channel0Output = 0;
        channel1Output = 0;
    }

    void process(float inL, float inR, float& outL, float& outR,
                 float roomSize, float decayTime, float dampingFreq, float width)
    {
        const int channel = 0;

        // Map parameters to match RSAlgorithmicVerb
        // Damping: 0-1 maps to 20000-200 Hz (inverted - higher value = more damping)
        float dampingCutoff = 200.0f + (1.0f - dampingFreq) * 19800.0f;
        dampingFilter1.setCutoffFrequency(dampingCutoff);
        dampingFilter2.setCutoffFrequency(dampingCutoff);

        // Input filter at 13.5 kHz
        inputFilter.setCutoffFrequency(13500.0f);

        // Room size scales delay times (0.5 to 1.5 range)
        float size = 0.5f + roomSize * 1.0f;

        // Set input diffusion network delays
        allpass1.setDelay(210.0f * size);
        allpass2.setDelay(158.0f * size);
        allpass3.setDelay(561.0f * size);
        allpass4.setDelay(410.0f * size);

        // Set tank allpass delays
        allpass5.setDelay(3931.0f * size);
        allpass6.setDelay(2664.0f * size);

        // Decay tank allpasses (fixed delays, no modulation)
        float decayAPF1Delay = 1343.0f * size;
        float decayAPF2Delay = 995.0f * size;
        decayAPF1.setDelay(decayAPF1Delay);
        decayAPF2.setDelay(decayAPF2Delay);

        // Set tank delays
        delay1.setDelay(6241.0f * size);
        delay2.setDelay(4641.0f * size);
        delay3.setDelay(6590.0f * size);
        delay4.setDelay(5505.0f * size);

        // Mono sum input
        float input = (inL + inR) * 0.5f;

        // Input filter
        input = inputFilter.processSample(channel, input);

        // Input diffusion network
        // Diffusion coefficients from Dattorro paper
        float inputDiffusion1 = 0.75f;
        float inputDiffusion2 = 0.625f;

        allpassOutput = allpass1.popSample(channel);
        feedback = allpassOutput * inputDiffusion1;
        feedforward = -input - feedback;
        allpass1.pushSample(channel, input + feedback);
        input = allpassOutput + feedforward;

        allpassOutput = allpass2.popSample(channel);
        feedback = allpassOutput * inputDiffusion1;
        feedforward = -input - feedback;
        allpass2.pushSample(channel, input + feedback);
        input = allpassOutput + feedforward;

        allpassOutput = allpass3.popSample(channel);
        feedback = allpassOutput * inputDiffusion2;
        feedforward = -input - feedback;
        allpass3.pushSample(channel, input + feedback);
        input = allpassOutput + feedforward;

        allpassOutput = allpass4.popSample(channel);
        feedback = allpassOutput * inputDiffusion2;
        feedforward = -input - feedback;
        allpass4.pushSample(channel, input + feedback);
        input = allpassOutput + feedforward;

        // ===== TANK 1 (Left) =====
        input += summingB * decayTime;

        // Decay APF1 (decay diffusion 1)
        float decayDiffusion1 = 0.7f;  // Reduced from 0.93 for stability
        allpassOutput = decayAPF1.popSample(channel);
        feedback = allpassOutput * decayDiffusion1;
        feedforward = -input - feedback;
        decayAPF1.pushSample(channel, input + feedback);
        input = allpassOutput + feedforward;

        // Delay 1
        delay1.pushSample(channel, input);
        input = dampingFilter1.processSample(channel, delay1.popSample(channel));

        // OUTPUT NODE A
        channel0Output = delay1.getSampleAtDelay(channel, 394 * size) * 0.6f;
        channel0Output += delay1.getSampleAtDelay(channel, 4401 * size) * 0.6f;
        channel1Output = -delay1.getSampleAtDelay(channel, 3124 * size) * 0.6f;

        // APF5
        allpassOutput = allpass5.popSample(channel);
        feedback = allpassOutput * decayDiffusion1;
        feedforward = -input - feedback;
        allpass5.pushSample(channel, input + feedback);
        input = allpassOutput + feedforward;

        // OUTPUT NODE B
        channel0Output -= allpass5.getSampleAtDelay(channel, 2831 * size) * 0.6f;
        channel1Output -= allpass5.getSampleAtDelay(channel, 496 * size) * 0.6f;

        // Delay 2
        delay2.pushSample(channel, input);
        input = delay2.popSample(channel);

        // OUTPUT NODE C
        channel0Output += delay2.getSampleAtDelay(channel, 2954 * size) * 0.6f;
        channel1Output -= delay2.getSampleAtDelay(channel, 179 * size) * 0.6f;

        summingA = input;

        // ===== TANK 2 (Right) =====
        input += summingA * decayTime;

        // Decay APF2 (decay diffusion 2)
        float decayDiffusion2 = 0.5f;  // Reduced from 0.67 for stability
        allpassOutput = decayAPF2.popSample(channel);
        feedback = allpassOutput * decayDiffusion2;
        feedforward = -input - feedback;
        decayAPF2.pushSample(channel, input + feedback);
        input = allpassOutput + feedforward;

        // Delay 3
        delay3.pushSample(channel, input);
        input = dampingFilter2.processSample(channel, delay3.popSample(channel));

        // OUTPUT NODE D
        channel0Output -= delay3.getSampleAtDelay(channel, 2945 * size) * 0.6f;
        channel1Output += delay3.getSampleAtDelay(channel, 522 * size) * 0.6f;
        channel1Output += delay3.getSampleAtDelay(channel, 5368 * size) * 0.6f;

        // APF6
        allpassOutput = allpass6.popSample(channel);
        feedback = allpassOutput * decayDiffusion2;
        feedforward = -input - feedback;
        allpass6.pushSample(channel, input + feedback);
        input = allpassOutput + feedforward;

        // OUTPUT NODE E
        channel0Output -= allpass6.getSampleAtDelay(channel, 277 * size) * 0.6f;
        channel1Output -= allpass6.getSampleAtDelay(channel, 1817 * size) * 0.6f;

        // Delay 4
        delay4.pushSample(channel, input);
        input = delay4.popSample(channel);

        summingB = input;

        // OUTPUT NODE F
        channel0Output -= delay4.getSampleAtDelay(channel, 1578 * size) * 0.6f;
        channel1Output += delay4.getSampleAtDelay(channel, 3956 * size) * 0.6f;

        // Apply width (M/S processing)
        float mid = (channel0Output + channel1Output) * 0.5f;
        float side = (channel0Output - channel1Output) * 0.5f * width;
        outL = mid + side;
        outR = mid - side;
    }

private:
    // Input diffusion allpasses
    juce::dsp::DelayLine<float> allpass1 {22050};
    juce::dsp::DelayLine<float> allpass2 {22050};
    juce::dsp::DelayLine<float> allpass3 {22050};
    juce::dsp::DelayLine<float> allpass4 {22050};
    DelayLineWithSampleAccess<float> allpass5 {22050};
    DelayLineWithSampleAccess<float> allpass6 {22050};

    // Decay tank allpasses (fixed delays, no modulation)
    juce::dsp::DelayLine<float> decayAPF1 {22050};
    juce::dsp::DelayLine<float> decayAPF2 {22050};

    // Tank delays
    DelayLineWithSampleAccess<float> delay1 {22050};
    DelayLineWithSampleAccess<float> delay2 {22050};
    DelayLineWithSampleAccess<float> delay3 {22050};
    DelayLineWithSampleAccess<float> delay4 {22050};

    // Filters
    juce::dsp::FirstOrderTPTFilter<float> inputFilter;
    juce::dsp::FirstOrderTPTFilter<float> dampingFilter1;
    juce::dsp::FirstOrderTPTFilter<float> dampingFilter2;

    // Processing variables
    float allpassOutput = 0.0f;
    float feedback = 0.0f;
    float feedforward = 0.0f;
    float summingA = 0.0f;
    float summingB = 0.0f;
    float channel0Output = 0.0f;
    float channel1Output = 0.0f;
};

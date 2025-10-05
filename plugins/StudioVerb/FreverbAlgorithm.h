/*
  ==============================================================================

    FreverbAlgorithm.h
    Created: 2025
    Author:  Luna Co. Audio

    Freeverb - Schroeder/Moorer Reverb (Public Domain Algorithm)
    Original by "Jezar at Dreampoint" - http://www.dreampoint.co.uk

    8 parallel comb filters + 4 series allpass filters
    Proven stable algorithm used worldwide

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>

class FreverbAlgorithm
{
public:
    FreverbAlgorithm()
    {
        reset();
    }

    void prepare(double sampleRate, int samplesPerBlock)
    {
        currentSampleRate = sampleRate;

        // Scale delay lengths from original 44100 Hz to current sample rate
        float scale = static_cast<float>(sampleRate / 44100.0);

        // Comb filter delay lengths (8 parallel combs per channel)
        const int combLengths[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        const int stereospread = 23;  // Right channel offset

        juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1};

        // Prepare predelay buffers
        preDelayL.prepare(spec);
        preDelayL.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2));  // 200ms max
        preDelayR.prepare(spec);
        preDelayR.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2));

        for (int i = 0; i < 8; ++i)
        {
            combsL[i].prepare(spec);
            combsL[i].setMaximumDelayInSamples(static_cast<int>(combLengths[i] * scale) + 10);
            combsR[i].prepare(spec);
            combsR[i].setMaximumDelayInSamples(static_cast<int>((combLengths[i] + stereospread) * scale) + 10);
        }

        // Allpass filter delay lengths (4 series allpasses)
        const int allpassLengths[4] = {556, 441, 341, 225};

        for (int i = 0; i < 4; ++i)
        {
            allpassL[i].prepare(spec);
            allpassL[i].setMaximumDelayInSamples(static_cast<int>(allpassLengths[i] * scale) + 10);
            allpassR[i].prepare(spec);
            allpassR[i].setMaximumDelayInSamples(static_cast<int>((allpassLengths[i] + stereospread) * scale) + 10);
        }

        reset();
    }

    void reset()
    {
        preDelayL.reset();
        preDelayR.reset();

        for (int i = 0; i < 8; ++i)
        {
            combsL[i].reset();
            combsR[i].reset();
            combFilterStateL[i] = 0.0f;
            combFilterStateR[i] = 0.0f;
        }

        for (int i = 0; i < 4; ++i)
        {
            allpassL[i].reset();
            allpassR[i].reset();
        }
    }

    void process(float inL, float inR, float& outL, float& outR,
                 float size, float decay, float damping, float predelayMs)
    {
        // Apply predelay
        int predelaySamples = static_cast<int>(predelayMs * 0.001f * currentSampleRate);
        preDelayL.pushSample(0, inL);
        preDelayR.pushSample(0, inR);

        float delayedL = preDelayL.popSample(0, predelaySamples, true);
        float delayedR = preDelayR.popSample(0, predelaySamples, true);

        // Freeverb uses mono input
        float input = (delayedL + delayedR) * 0.5f;

        // Fixed gain from original Freeverb
        input *= 0.015f;

        // Room size parameter (0.7 to 0.98)
        float roomsize = 0.70f + size * 0.28f;

        // Damping coefficient (0 to 0.4)
        float damp = damping * 0.4f;
        float damp1 = damp;
        float damp2 = 1.0f - damp;

        // Apply decay to roomsize
        roomsize *= decay;

        // Process 8 parallel comb filters for LEFT channel
        float combOutL = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            float delayed = combsL[i].popSample(0, combsL[i].getDelay(), true);

            // One-pole lowpass filter (damping)
            combFilterStateL[i] = delayed * damp2 + combFilterStateL[i] * damp1;

            // Feedback
            combsL[i].pushSample(0, input + combFilterStateL[i] * roomsize);

            combOutL += delayed;
        }

        // Process 8 parallel comb filters for RIGHT channel
        float combOutR = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            float delayed = combsR[i].popSample(0, combsR[i].getDelay(), true);

            // One-pole lowpass filter (damping)
            combFilterStateR[i] = delayed * damp2 + combFilterStateR[i] * damp1;

            // Feedback
            combsR[i].pushSample(0, input + combFilterStateR[i] * roomsize);

            combOutR += delayed;
        }

        // Process 4 series allpass filters for LEFT channel
        outL = combOutL;
        for (int i = 0; i < 4; ++i)
        {
            outL = processAllpass(allpassL[i], outL, 0.5f);
        }

        // Process 4 series allpass filters for RIGHT channel
        outR = combOutR;
        for (int i = 0; i < 4; ++i)
        {
            outR = processAllpass(allpassR[i], outR, 0.5f);
        }

        // Output scaling (wet level)
        outL *= 3.0f;
        outR *= 3.0f;
    }

private:
    float processAllpass(juce::dsp::DelayLine<float>& delay, float input, float gain)
    {
        float delayed = delay.popSample(0, delay.getDelay(), true);
        float output = -input + delayed;
        delay.pushSample(0, input + delayed * gain);
        return output;
    }

    double currentSampleRate = 48000.0;

    // Predelay buffers
    juce::dsp::DelayLine<float> preDelayL{48000};
    juce::dsp::DelayLine<float> preDelayR{48000};

    // 8 comb filters per channel
    std::array<juce::dsp::DelayLine<float>, 8> combsL;
    std::array<juce::dsp::DelayLine<float>, 8> combsR;

    // Comb filter damping state (one-pole lowpass)
    std::array<float, 8> combFilterStateL{};
    std::array<float, 8> combFilterStateR{};

    // 4 allpass filters per channel
    std::array<juce::dsp::DelayLine<float>, 4> allpassL;
    std::array<juce::dsp::DelayLine<float>, 4> allpassR;
};

#include "SimpleFreeverb.h"

SimpleFreeverb::SimpleFreeverb()
{
    // Initialize with default sizes
    for (int i = 0; i < numCombs; ++i) {
        combL[i].setSize(combTuning[i]);
        combR[i].setSize(combTuning[i] + 23); // Stereo spread
    }

    for (int i = 0; i < numAllpasses; ++i) {
        allpassL[i].setSize(allpassTuning[i]);
        allpassR[i].setSize(allpassTuning[i] + 23); // Stereo spread
    }

    updateDamping();
}

void SimpleFreeverb::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Scale buffer sizes for different sample rates
    float ratio = static_cast<float>(sampleRate) / 44100.0f;

    for (int i = 0; i < numCombs; ++i) {
        combL[i].setSize(static_cast<int>(combTuning[i] * ratio));
        combR[i].setSize(static_cast<int>((combTuning[i] + 23) * ratio));
    }

    for (int i = 0; i < numAllpasses; ++i) {
        allpassL[i].setSize(static_cast<int>(allpassTuning[i] * ratio));
        allpassR[i].setSize(static_cast<int>((allpassTuning[i] + 23) * ratio));
    }

    reset();
}

void SimpleFreeverb::reset()
{
    for (auto& comb : combL) comb.clear();
    for (auto& comb : combR) comb.clear();
    for (auto& allpass : allpassL) allpass.clear();
    for (auto& allpass : allpassR) allpass.clear();
}

void SimpleFreeverb::updateDamping()
{
    for (auto& comb : combL) {
        comb.setFeedback(roomSize);
        comb.setDamp(damp);
    }

    for (auto& comb : combR) {
        comb.setFeedback(roomSize);
        comb.setDamp(damp);
    }
}

void SimpleFreeverb::processBlock(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numChannels < 2)
        return;

    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);

    for (int i = 0; i < numSamples; ++i)
    {
        float inputL = leftChannel[i];
        float inputR = rightChannel[i];

        // Mix to mono and scale
        float input = (inputL + inputR) * fixedGain;

        // Accumulate comb filters in parallel
        float outL = 0.0f;
        float outR = 0.0f;

        for (auto& comb : combL)
            outL += comb.process(input);

        for (auto& comb : combR)
            outR += comb.process(input);

        // Feed through allpasses in series
        for (auto& allpass : allpassL)
            outL = allpass.process(outL);

        for (auto& allpass : allpassR)
            outR = allpass.process(outR);

        // Apply wet/dry mix and width
        float wet1 = wet * (width * 0.5f + 0.5f);
        float wet2 = wet * ((1.0f - width) * 0.5f);

        leftChannel[i] = outL * wet1 + outR * wet2 + inputL * dry;
        rightChannel[i] = outR * wet1 + outL * wet2 + inputR * dry;

        // Safety limiting
        leftChannel[i] = juce::jlimit(-1.0f, 1.0f, leftChannel[i]);
        rightChannel[i] = juce::jlimit(-1.0f, 1.0f, rightChannel[i]);
    }
}
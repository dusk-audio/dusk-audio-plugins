/*
  ==============================================================================

    DattorroReverb.h
    Created: 2025
    Author:  Luna Co. Audio

    Dattorro Plate Reverb - Industry Standard Implementation
    Based on "Effect Design, Part 1: Reverberator and Other Filters"
    by Jon Dattorro, J. Audio Eng. Soc., Vol 45, No. 9, 1997 September

    This is a proven, stable reverb algorithm used in countless professional plugins.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>

class DattorroReverb
{
public:
    DattorroReverb()
    {
        reset();
    }

    void prepare(double sampleRate, int samplesPerBlock)
    {
        currentSampleRate = sampleRate;

        // Prepare damping filters
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 1;

        // All delay lines need the same spec
        preDelayL.prepare(spec);
        preDelayR.prepare(spec);

        inputDiffusion1L.prepare(spec);
        inputDiffusion2L.prepare(spec);
        inputDiffusion3L.prepare(spec);
        inputDiffusion4L.prepare(spec);

        decayDiffusion1L.prepare(spec);
        delay1L.prepare(spec);
        decayDiffusion2L.prepare(spec);
        delay2L.prepare(spec);

        decayDiffusion1R.prepare(spec);
        delay1R.prepare(spec);
        decayDiffusion2R.prepare(spec);
        delay2R.prepare(spec);

        dampingFilterL.prepare(spec);
        dampingFilterL.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

        dampingFilterR.prepare(spec);
        dampingFilterR.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

        // Set max delay lengths
        float scale = static_cast<float>(sampleRate / 29761.0);

        preDelayL.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2));
        preDelayR.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2));

        inputDiffusion1L.setMaximumDelayInSamples(static_cast<int>(142 * scale));
        inputDiffusion2L.setMaximumDelayInSamples(static_cast<int>(107 * scale));
        inputDiffusion3L.setMaximumDelayInSamples(static_cast<int>(379 * scale));
        inputDiffusion4L.setMaximumDelayInSamples(static_cast<int>(277 * scale));

        decayDiffusion1L.setMaximumDelayInSamples(static_cast<int>(672 * scale));
        delay1L.setMaximumDelayInSamples(static_cast<int>(4453 * scale));
        decayDiffusion2L.setMaximumDelayInSamples(static_cast<int>(1800 * scale));
        delay2L.setMaximumDelayInSamples(static_cast<int>(3720 * scale));

        decayDiffusion1R.setMaximumDelayInSamples(static_cast<int>(908 * scale));
        delay1R.setMaximumDelayInSamples(static_cast<int>(4217 * scale));
        decayDiffusion2R.setMaximumDelayInSamples(static_cast<int>(2656 * scale));
        delay2R.setMaximumDelayInSamples(static_cast<int>(3163 * scale));

        reset();
    }

    void reset()
    {
        preDelayL.reset();
        preDelayR.reset();

        inputDiffusion1L.reset();
        inputDiffusion2L.reset();
        inputDiffusion3L.reset();
        inputDiffusion4L.reset();

        decayDiffusion1L.reset();
        delay1L.reset();
        decayDiffusion2L.reset();
        delay2L.reset();

        decayDiffusion1R.reset();
        delay1R.reset();
        decayDiffusion2R.reset();
        delay2R.reset();

        dampingFilterL.reset();
        dampingFilterR.reset();

        writePos = 0;

        tankOutputL = 0.0f;
        tankOutputR = 0.0f;
    }

    void process(float inL, float inR, float& outL, float& outR,
                 float size, float decay, float damping, float predelayMs)
    {
        // Pre-delay
        int predelaySamples = static_cast<int>(predelayMs * 0.001f * currentSampleRate);
        preDelayL.pushSample(0, inL);
        preDelayR.pushSample(0, inR);

        float delayedL = preDelayL.popSample(0, predelaySamples, true);
        float delayedR = preDelayR.popSample(0, predelaySamples, true);

        // Mono sum for input
        float monoInput = (delayedL + delayedR) * 0.5f;

        // Pre-filter (optional bandwidth control)
        float filteredInput = monoInput * 0.75f;  // Input gain

        // Input diffusion network (4 allpass filters in series)
        // Use mono input for both channels (as per Dattorro paper)
        float diffused = filteredInput;

        // Input diffusion constants from Dattorro paper
        const float inputDiffusion1 = 0.75f;
        const float inputDiffusion2 = 0.625f;

        // Single diffusion path (mono)
        diffused = processAllpass(inputDiffusion1L, diffused, inputDiffusion1);
        diffused = processAllpass(inputDiffusion2L, diffused, inputDiffusion1);
        diffused = processAllpass(inputDiffusion3L, diffused, inputDiffusion2);
        diffused = processAllpass(inputDiffusion4L, diffused, inputDiffusion2);

        // Feed into tank with cross-coupling
        float tankInputL = diffused + tankOutputR * decay;
        float tankInputR = diffused + tankOutputL * decay;

        // Update damping filter cutoff based on damping parameter
        float dampingFreq = 500.0f + (1.0f - damping) * 10000.0f;  // 500Hz to 10.5kHz
        dampingFilterL.setCutoffFrequency(dampingFreq);
        dampingFilterR.setCutoffFrequency(dampingFreq);

        // Decay diffusion constants
        float decayDiffusion2_gain = juce::jmap(size, 0.0f, 1.0f, 0.25f, 0.50f);

        // ======== LEFT TANK ========
        float tankL = tankInputL;
        tankL = processAllpass(decayDiffusion1L, tankL, 0.70f);

        // Store current delay output BEFORE writing new sample
        float d1L_out = delay1L.popSample(0);
        delay1L.pushSample(0, tankL);

        tankL = dampingFilterL.processSample(0, d1L_out);
        tankL = processAllpass(decayDiffusion2L, tankL, -decayDiffusion2_gain);

        // Store current delay output BEFORE writing new sample
        float d2L_out = delay2L.popSample(0);
        delay2L.pushSample(0, tankL);

        tankOutputL = d2L_out * decay;

        // ======== RIGHT TANK ========
        float tankR = tankInputR;
        tankR = processAllpass(decayDiffusion1R, tankR, 0.70f);

        // Store current delay output BEFORE writing new sample
        float d1R_out = delay1R.popSample(0);
        delay1R.pushSample(0, tankR);

        tankR = dampingFilterR.processSample(0, d1R_out);
        tankR = processAllpass(decayDiffusion2R, tankR, -decayDiffusion2_gain);

        // Store current delay output BEFORE writing new sample
        float d2R_out = delay2R.popSample(0);
        delay2R.pushSample(0, tankR);

        tankOutputR = d2R_out * decay;

        // Simplified stereo output - uses tank outputs with cross-coupling
        // This creates natural stereo width while avoiding multi-tap read bugs
        outL = (d1L_out + d2L_out - d1R_out * 0.5f) * 0.6f;
        outR = (d1R_out + d2R_out - d1L_out * 0.5f) * 0.6f;
    }

private:
    float processAllpass(juce::dsp::DelayLine<float>& delay, float input, float gain)
    {
        float delayed = delay.popSample(0, delay.getDelay(), true);
        float output = -input + delayed;
        delay.pushSample(0, input + (gain * delayed)); // Correct feedback path
        return output;
    }

    double currentSampleRate = 48000.0;
    int writePos = 0;

    // Tank feedback state
    float tankOutputL = 0.0f;
    float tankOutputR = 0.0f;

    // Pre-delay
    juce::dsp::DelayLine<float> preDelayL{48000};
    juce::dsp::DelayLine<float> preDelayR{48000};

    // Input diffusion (4 allpass filters, mono path)
    juce::dsp::DelayLine<float> inputDiffusion1L{1024};
    juce::dsp::DelayLine<float> inputDiffusion2L{1024};
    juce::dsp::DelayLine<float> inputDiffusion3L{1024};
    juce::dsp::DelayLine<float> inputDiffusion4L{1024};

    // Left tank
    juce::dsp::DelayLine<float> decayDiffusion1L{4096};
    juce::dsp::DelayLine<float> delay1L{24000};
    juce::dsp::DelayLine<float> decayDiffusion2L{8192};
    juce::dsp::DelayLine<float> delay2L{24000};

    // Right tank
    juce::dsp::DelayLine<float> decayDiffusion1R{4096};
    juce::dsp::DelayLine<float> delay1R{24000};
    juce::dsp::DelayLine<float> decayDiffusion2R{8192};
    juce::dsp::DelayLine<float> delay2R{24000};

    // Damping filters
    juce::dsp::StateVariableTPTFilter<float> dampingFilterL;
    juce::dsp::StateVariableTPTFilter<float> dampingFilterR;
};

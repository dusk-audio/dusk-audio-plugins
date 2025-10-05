/*
  ==============================================================================

    DattorroPlate.h
    Created: 2025
    Author:  Luna Co. Audio

    Professional Dattorro Plate Reverb Implementation
    Based on "Effect Design, Part 1: Reverberator and Other Filters"
    by Jon Dattorro, J. Audio Eng. Soc., Vol 45, No. 9, 1997 September

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

class DattorroPlate
{
public:
    DattorroPlate() = default;

    void prepare(double sampleRate, int samplesPerBlock)
    {
        fs = sampleRate;

        // Calculate scaling factor for delay times (based on 29.761kHz reference)
        scale = static_cast<float>(sampleRate / 29761.0);

        // Pre-delay
        preDelay.resize(static_cast<size_t>(sampleRate * 0.5)); // 500ms max

        // Input diffusion network (4 allpass filters)
        inputAPF1.resize(static_cast<size_t>(142 * scale + 1));
        inputAPF2.resize(static_cast<size_t>(107 * scale + 1));
        inputAPF3.resize(static_cast<size_t>(379 * scale + 1));
        inputAPF4.resize(static_cast<size_t>(277 * scale + 1));

        // Left tank
        leftAPF1.resize(static_cast<size_t>(672 * scale + 1));
        leftDelay1.resize(static_cast<size_t>(4453 * scale + 1));
        leftAPF2.resize(static_cast<size_t>(1800 * scale + 1));
        leftDelay2.resize(static_cast<size_t>(3720 * scale + 1));

        // Right tank
        rightAPF1.resize(static_cast<size_t>(908 * scale + 1));
        rightDelay1.resize(static_cast<size_t>(4217 * scale + 1));
        rightAPF2.resize(static_cast<size_t>(2656 * scale + 1));
        rightDelay2.resize(static_cast<size_t>(3163 * scale + 1));

        reset();
    }

    void reset()
    {
        std::fill(preDelay.begin(), preDelay.end(), 0.0f);

        std::fill(inputAPF1.begin(), inputAPF1.end(), 0.0f);
        std::fill(inputAPF2.begin(), inputAPF2.end(), 0.0f);
        std::fill(inputAPF3.begin(), inputAPF3.end(), 0.0f);
        std::fill(inputAPF4.begin(), inputAPF4.end(), 0.0f);

        std::fill(leftAPF1.begin(), leftAPF1.end(), 0.0f);
        std::fill(leftDelay1.begin(), leftDelay1.end(), 0.0f);
        std::fill(leftAPF2.begin(), leftAPF2.end(), 0.0f);
        std::fill(leftDelay2.begin(), leftDelay2.end(), 0.0f);

        std::fill(rightAPF1.begin(), rightAPF1.end(), 0.0f);
        std::fill(rightDelay1.begin(), rightDelay1.end(), 0.0f);
        std::fill(rightAPF2.begin(), rightAPF2.end(), 0.0f);
        std::fill(rightDelay2.begin(), rightDelay2.end(), 0.0f);

        preDelayIndex = 0;

        inputAPF1_idx = 0;
        inputAPF2_idx = 0;
        inputAPF3_idx = 0;
        inputAPF4_idx = 0;

        leftAPF1_idx = 0;
        leftDelay1_idx = 0;
        leftAPF2_idx = 0;
        leftDelay2_idx = 0;

        rightAPF1_idx = 0;
        rightDelay1_idx = 0;
        rightAPF2_idx = 0;
        rightDelay2_idx = 0;

        leftLPF = 0.0f;
        rightLPF = 0.0f;
    }

    void process(float inL, float inR, float& outL, float& outR,
                 float size, float decay, float damping, float predelayMs, float width)
    {
        // Clamp parameters
        size = juce::jlimit(0.0f, 1.0f, size);
        decay = juce::jlimit(0.0f, 1.0f, decay);
        damping = juce::jlimit(0.0f, 1.0f, damping);
        width = juce::jlimit(0.0f, 1.0f, width);

        // Pre-delay
        int preDelaySamples = static_cast<int>(predelayMs * 0.001f * fs);
        preDelaySamples = juce::jlimit(0, static_cast<int>(preDelay.size()) - 1, preDelaySamples);

        float mono = (inL + inR) * 0.5f;

        // Write to pre-delay
        preDelay[preDelayIndex] = mono;

        // Read from pre-delay
        int readIdx = (preDelayIndex - preDelaySamples + preDelay.size()) % preDelay.size();
        float delayedInput = preDelay[readIdx];

        preDelayIndex = (preDelayIndex + 1) % preDelay.size();

        // Input diffusion (4 allpass filters in series)
        float diffused = delayedInput * 0.75f; // Input gain

        diffused = processAllpass(inputAPF1, inputAPF1_idx, diffused, 0.75f);
        diffused = processAllpass(inputAPF2, inputAPF2_idx, diffused, 0.75f);
        diffused = processAllpass(inputAPF3, inputAPF3_idx, diffused, 0.625f);
        diffused = processAllpass(inputAPF4, inputAPF4_idx, diffused, 0.625f);

        // Calculate decay coefficient
        float decayCoeff = decay;

        // Calculate damping coefficient for one-pole lowpass
        float dampingCoeff = 1.0f - damping;

        // Figure-8 tank
        // Left tank input = diffused input + feedback from right tank
        float leftInput = diffused + rightDelay2[rightDelay2_idx] * decayCoeff;

        // Left tank processing
        float leftOut1 = processAllpass(leftAPF1, leftAPF1_idx, leftInput, -0.7f);

        leftDelay1[leftDelay1_idx] = leftOut1;
        leftDelay1_idx = (leftDelay1_idx + 1) % leftDelay1.size();

        float leftDelayed = leftDelay1[leftDelay1_idx];

        // Damping filter (one-pole lowpass)
        leftLPF = leftDelayed * dampingCoeff + leftLPF * damping;

        float leftOut2 = processAllpass(leftAPF2, leftAPF2_idx, leftLPF, 0.5f);

        leftDelay2[leftDelay2_idx] = leftOut2;
        leftDelay2_idx = (leftDelay2_idx + 1) % leftDelay2.size();

        // Right tank input = diffused input + feedback from left tank
        float rightInput = diffused + leftDelay2[leftDelay2_idx] * decayCoeff;

        // Right tank processing
        float rightOut1 = processAllpass(rightAPF1, rightAPF1_idx, rightInput, -0.7f);

        rightDelay1[rightDelay1_idx] = rightOut1;
        rightDelay1_idx = (rightDelay1_idx + 1) % rightDelay1.size();

        float rightDelayed = rightDelay1[rightDelay1_idx];

        // Damping filter (one-pole lowpass)
        rightLPF = rightDelayed * dampingCoeff + rightLPF * damping;

        float rightOut2 = processAllpass(rightAPF2, rightAPF2_idx, rightLPF, 0.5f);

        rightDelay2[rightDelay2_idx] = rightOut2;
        rightDelay2_idx = (rightDelay2_idx + 1) % rightDelay2.size();

        // Output taps (as per Dattorro paper)
        // Left output
        outL = 0.6f * leftDelay1[(leftDelay1_idx + static_cast<int>(266 * scale)) % leftDelay1.size()]
             + 0.6f * leftDelay1[(leftDelay1_idx + static_cast<int>(2974 * scale)) % leftDelay1.size()]
             - 0.6f * leftAPF2[(leftAPF2_idx + static_cast<int>(1913 * scale)) % leftAPF2.size()]
             + 0.6f * leftDelay2[(leftDelay2_idx + static_cast<int>(1996 * scale)) % leftDelay2.size()]
             - 0.6f * rightDelay1[(rightDelay1_idx + static_cast<int>(353 * scale)) % rightDelay1.size()]
             - 0.6f * rightAPF2[(rightAPF2_idx + static_cast<int>(1990 * scale)) % rightAPF2.size()];

        // Right output
        outR = 0.6f * rightDelay1[(rightDelay1_idx + static_cast<int>(353 * scale)) % rightDelay1.size()]
             + 0.6f * rightDelay1[(rightDelay1_idx + static_cast<int>(3627 * scale)) % rightDelay1.size()]
             - 0.6f * rightAPF2[(rightAPF2_idx + static_cast<int>(1990 * scale)) % rightAPF2.size()]
             + 0.6f * rightDelay2[(rightDelay2_idx + static_cast<int>(1066 * scale)) % rightDelay2.size()]
             - 0.6f * leftDelay1[(leftDelay1_idx + static_cast<int>(266 * scale)) % leftDelay1.size()]
             - 0.6f * leftAPF2[(leftAPF2_idx + static_cast<int>(1913 * scale)) % leftAPF2.size()];

        // Apply width control
        float mid = (outL + outR) * 0.5f;
        float side = (outL - outR) * 0.5f * width;

        outL = mid + side;
        outR = mid - side;
    }

private:
    float processAllpass(std::vector<float>& buffer, size_t& index, float input, float gain)
    {
        float buffered = buffer[index];
        float output = -input + buffered;
        buffer[index] = input + gain * buffered;
        index = (index + 1) % buffer.size();
        return output;
    }

    double fs = 48000.0;
    float scale = 1.0f;

    // Pre-delay
    std::vector<float> preDelay;
    size_t preDelayIndex = 0;

    // Input diffusion network
    std::vector<float> inputAPF1, inputAPF2, inputAPF3, inputAPF4;
    size_t inputAPF1_idx = 0, inputAPF2_idx = 0, inputAPF3_idx = 0, inputAPF4_idx = 0;

    // Left tank
    std::vector<float> leftAPF1, leftDelay1, leftAPF2, leftDelay2;
    size_t leftAPF1_idx = 0, leftDelay1_idx = 0, leftAPF2_idx = 0, leftDelay2_idx = 0;
    float leftLPF = 0.0f;

    // Right tank
    std::vector<float> rightAPF1, rightDelay1, rightAPF2, rightDelay2;
    size_t rightAPF1_idx = 0, rightDelay1_idx = 0, rightAPF2_idx = 0, rightDelay2_idx = 0;
    float rightLPF = 0.0f;
};

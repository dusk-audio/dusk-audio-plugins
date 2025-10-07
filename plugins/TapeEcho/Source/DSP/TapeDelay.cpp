#include "TapeDelay.h"
#include <cmath>

TapeDelay::TapeDelay()
{
    // Initialize default head delays (evenly spaced)
    heads[0].delayMs = 70.0f;   // Short delay
    heads[1].delayMs = 150.0f;  // Medium delay
    heads[2].delayMs = 300.0f;  // Long delay
}

void TapeDelay::prepare(double sr, int maxBlockSize)
{
    sampleRate = static_cast<float>(sr);

    // Allocate delay buffers (up to 1 second of delay per channel)
    bufferSize = static_cast<int>(std::ceil(sampleRate * MAX_DELAY_MS / 1000.0f));
    delayBufferL.resize(bufferSize);
    delayBufferR.resize(bufferSize);

    reset();
    updateFilters();
}

void TapeDelay::reset()
{
    std::fill(delayBufferL.begin(), delayBufferL.end(), 0.0f);
    std::fill(delayBufferR.begin(), delayBufferR.end(), 0.0f);

    writePosition = 0;
    lfoPhase = 0.0f;
    lastOutputL = 0.0f;
    lastOutputR = 0.0f;

    for (auto& head : heads)
    {
        head.smoothedDelay = head.delayMs * sampleRate / 1000.0f;
    }

    lowpassL.reset();
    lowpassR.reset();
    highpassL.reset();
    highpassR.reset();
}

void TapeDelay::setDelayTime(int head, float delayMs)
{
    if (head >= 0 && head < NUM_HEADS)
    {
        heads[head].delayMs = juce::jlimit(10.0f, MAX_DELAY_MS, delayMs);
        heads[head].delaySamples = heads[head].delayMs * sampleRate / 1000.0f;
    }
}

void TapeDelay::setFeedback(float fb)
{
    feedback = juce::jlimit(0.0f, 0.99f, fb);
}

void TapeDelay::setWowFlutter(float amount, float rate, int shape)
{
    wowFlutterAmount = juce::jlimit(0.0f, 1.0f, amount);
    wowFlutterRate = juce::jlimit(0.1f, 10.0f, rate);
    lfoShape = juce::jlimit(0, 5, shape);
}

void TapeDelay::setTapeAge(float age)
{
    tapeAge = juce::jlimit(0.0f, 1.0f, age);

    // Tape age affects noise level and frequency response
    noiseLevel = age * 0.001f;
    highFreqDamping = 3000.0f + (1.0f - age) * 12000.0f;

    updateFilters();
}

void TapeDelay::setHeadEnabled(int head, bool enabled)
{
    if (head >= 0 && head < NUM_HEADS)
    {
        heads[head].enabled = enabled;
    }
}

float TapeDelay::getLFOValue()
{
    float value = 0.0f;

    switch (lfoShape)
    {
        case Sine:
            value = std::sin(lfoPhase * 2.0f * juce::MathConstants<float>::pi);
            break;

        case Triangle:
            value = 2.0f * std::abs(2.0f * (lfoPhase - std::floor(lfoPhase + 0.5f))) - 1.0f;
            break;

        case Square:
            value = lfoPhase < 0.5f ? 1.0f : -1.0f;
            break;

        case SawUp:
            value = 2.0f * lfoPhase - 1.0f;
            break;

        case SawDown:
            value = 1.0f - 2.0f * lfoPhase;
            break;

        case Random:
            if (lfoPhase < 0.05f)
            {
                targetRandomValue = random.nextFloat() * 2.0f - 1.0f;
            }
            randomValue += (targetRandomValue - randomValue) * 0.1f;
            value = randomValue;
            break;
    }

    return value * wowFlutterAmount;
}

float TapeDelay::getInterpolatedSample(const std::vector<float>& buffer, float delaySamples)
{
    const int delayInt = static_cast<int>(delaySamples);
    const float fraction = delaySamples - delayInt;

    int readPos = writePosition - delayInt;
    if (readPos < 0) readPos += bufferSize;

    int nextPos = readPos - 1;
    if (nextPos < 0) nextPos += bufferSize;

    // Linear interpolation
    return buffer[readPos] * (1.0f - fraction) + buffer[nextPos] * fraction;
}

float TapeDelay::processSample(float input, float externalFeedback, int channel)
{
    float output = 0.0f;

    // Update LFO phase (only on channel 0 to avoid double updates)
    if (channel == 0)
    {
        lfoPhase += wowFlutterRate / sampleRate;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
    }

    // Get modulation value for wow and flutter
    float modulation = getLFOValue();

    // Mix delayed signals from all active heads
    for (auto& head : heads)
    {
        if (!head.enabled) continue;

        // Apply wow and flutter modulation to delay time
        float modulatedDelay = head.delaySamples * (1.0f + modulation * 0.02f);

        // Smooth delay changes to avoid clicks (only on channel 0)
        if (channel == 0)
        {
            head.smoothedDelay += (modulatedDelay - head.smoothedDelay) * 0.001f;
        }

        // Get delayed sample with interpolation
        if (channel == 0)
        {
            output += getInterpolatedSample(delayBufferL, head.smoothedDelay);
        }
        else
        {
            output += getInterpolatedSample(delayBufferR, head.smoothedDelay);
        }
    }

    // Add tape noise
    if (tapeAge > 0.0f)
    {
        output += random.nextFloat() * noiseLevel * 2.0f - noiseLevel;
    }

    // Apply feedback (with smoothing)
    // Note: externalFeedback has already been filtered by the Bass/Treble EQ
    smoothedFeedback += (feedback - smoothedFeedback) * 0.01f;
    float inputWithFeedback = input + externalFeedback * smoothedFeedback;

    // Apply tape coloration filters (age/damping only, not tone controls)
    if (channel == 0)
    {
        inputWithFeedback = lowpassL.processSingleSampleRaw(inputWithFeedback);
        inputWithFeedback = highpassL.processSingleSampleRaw(inputWithFeedback);

        // Write to delay buffer
        delayBufferL[writePosition] = inputWithFeedback;
        lastOutputL = output;

        // Update write position after channel 0 is written
        // This ensures it works for both mono (1 channel) and stereo (2 channels)
        writePosition++;
        if (writePosition >= bufferSize) writePosition = 0;
    }
    else
    {
        inputWithFeedback = lowpassR.processSingleSampleRaw(inputWithFeedback);
        inputWithFeedback = highpassR.processSingleSampleRaw(inputWithFeedback);

        // Write to delay buffer
        delayBufferR[writePosition] = inputWithFeedback;
        lastOutputR = output;
    }

    return output;
}

void TapeDelay::updateFilters()
{
    // Lowpass filter for high-frequency damping (tape age simulation)
    float lpFreq = juce::jlimit(200.0f, 20000.0f, highFreqDamping);
    lowpassL.setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, lpFreq));
    lowpassR.setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, lpFreq));

    // Highpass filter to remove DC and subsonic frequencies
    highpassL.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, 20.0f));
    highpassR.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, 20.0f));
}
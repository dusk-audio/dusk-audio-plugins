/*
  ==============================================================================

    DragonflyReverb.cpp
    Implementation of Dragonfly-style reverb algorithms

  ==============================================================================
*/

#include "DragonflyReverb.h"

//==============================================================================
// DelayLine implementation
void DragonflyReverb::DelayLine::setMaxSize(int maxSamples)
{
    buffer.resize(maxSamples);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    size = maxSamples;
    writePos = 0;
}

float DragonflyReverb::DelayLine::read(float delaySamples) const
{
    if (buffer.empty() || size == 0) return 0.0f;

    int readPos = writePos - static_cast<int>(delaySamples);
    while (readPos < 0) readPos += size;

    return buffer[readPos];
}

void DragonflyReverb::DelayLine::write(float sample)
{
    if (buffer.empty() || size == 0) return;

    buffer[writePos] = sample;
    writePos = (writePos + 1) % size;
}

void DragonflyReverb::DelayLine::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

//==============================================================================
// AllpassFilter implementation
void DragonflyReverb::AllpassFilter::setSize(int samples)
{
    buffer.resize(samples);
    bufferSize = samples;
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void DragonflyReverb::AllpassFilter::setFeedback(float g)
{
    feedback = juce::jlimit(0.0f, 0.98f, g);
}

float DragonflyReverb::AllpassFilter::process(float input)
{
    if (bufferSize == 0) return input;

    float delayed = buffer[writePos];
    float output = -input + delayed;

    buffer[writePos] = input + (delayed * feedback);
    writePos = (writePos + 1) % bufferSize;

    return output;
}

void DragonflyReverb::AllpassFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

//==============================================================================
// CombFilter implementation
void DragonflyReverb::CombFilter::setSize(int samples)
{
    buffer.resize(samples);
    bufferSize = samples;
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void DragonflyReverb::CombFilter::setFeedback(float g)
{
    feedback = juce::jlimit(0.0f, 0.98f, g);
}

void DragonflyReverb::CombFilter::setDamping(float d)
{
    damping = juce::jlimit(0.0f, 1.0f, d);
}

float DragonflyReverb::CombFilter::process(float input)
{
    if (bufferSize == 0) return 0.0f;

    float output = buffer[writePos];

    // Apply damping (simple lowpass)
    filterStore = output * (1.0f - damping) + filterStore * damping;

    buffer[writePos] = input + (filterStore * feedback);
    writePos = (writePos + 1) % bufferSize;

    return output;
}

void DragonflyReverb::CombFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    filterStore = 0.0f;
    writePos = 0;
}

//==============================================================================
// EarlyReflections implementation
void DragonflyReverb::EarlyReflections::prepare(double sr)
{
    sampleRate = sr;

    // Set up delay lines
    delays[0].setMaxSize(static_cast<int>(sampleRate * 0.1f)); // 100ms max
    delays[1].setMaxSize(static_cast<int>(sampleRate * 0.1f));

    // Initialize tap gains with exponential decay
    for (int i = 0; i < numTaps; ++i)
    {
        tapGains[i] = std::pow(0.8f, i * 0.3f) * (i % 2 == 0 ? 0.7f : -0.7f);
    }

    clear();
}

void DragonflyReverb::EarlyReflections::setRoomSize(float size)
{
    roomSize = juce::jlimit(0.1f, 2.0f, size);
}

void DragonflyReverb::EarlyReflections::process(const float* inputL, const float* inputR,
                                                float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Write input to delay lines
        delays[0].write(inputL[i]);
        delays[1].write(inputR[i]);

        // Sum early reflections
        float sumL = 0.0f;
        float sumR = 0.0f;

        for (int tap = 0; tap < numTaps; ++tap)
        {
            float delayTime = tapTimesMs[tap] * roomSize * sampleRate * 0.001f;
            float tapL = delays[0].read(delayTime);
            float tapR = delays[1].read(delayTime);

            // Cross-feed for stereo width
            sumL += tapL * tapGains[tap] * 0.6f + tapR * tapGains[tap] * 0.4f;
            sumR += tapR * tapGains[tap] * 0.6f + tapL * tapGains[tap] * 0.4f;
        }

        outputL[i] = sumL * 0.5f;
        outputR[i] = sumR * 0.5f;
    }
}

void DragonflyReverb::EarlyReflections::clear()
{
    delays[0].clear();
    delays[1].clear();
}

//==============================================================================
// RoomReverb (Progenitor2-style) implementation
void DragonflyReverb::RoomReverb::prepare(double sr)
{
    sampleRate = sr;

    // Set up comb filters with room-specific tunings
    for (int i = 0; i < numCombs; ++i)
    {
        int sizeL = static_cast<int>(combTuningsMs[i] * sampleRate * 0.001f);
        int sizeR = static_cast<int>(combTuningsMs[i] * sampleRate * 0.001f * 1.01f); // Slight stereo spread

        combsL[i].setSize(sizeL);
        combsR[i].setSize(sizeR);
        combsL[i].setFeedback(decayFeedback);
        combsR[i].setFeedback(decayFeedback);
        combsL[i].setDamping(damping);
        combsR[i].setDamping(damping);
    }

    // Set up allpass filters
    for (int i = 0; i < numAllpasses; ++i)
    {
        int sizeL = static_cast<int>(allpassTuningsMs[i] * sampleRate * 0.001f);
        int sizeR = static_cast<int>(allpassTuningsMs[i] * sampleRate * 0.001f * 1.01f);

        allpassesL[i].setSize(sizeL);
        allpassesR[i].setSize(sizeR);
        allpassesL[i].setFeedback(0.5f);
        allpassesR[i].setFeedback(0.5f);
    }

    clear();
}

void DragonflyReverb::RoomReverb::setDecayTime(float seconds)
{
    // Convert decay time to feedback
    decayFeedback = std::exp(-3.0f / (seconds * sampleRate / 1000.0f));
    decayFeedback = juce::jlimit(0.0f, 0.98f, decayFeedback);

    for (int i = 0; i < numCombs; ++i)
    {
        combsL[i].setFeedback(decayFeedback);
        combsR[i].setFeedback(decayFeedback);
    }
}

void DragonflyReverb::RoomReverb::setDamping(float damp)
{
    damping = juce::jlimit(0.0f, 1.0f, damp);

    for (int i = 0; i < numCombs; ++i)
    {
        combsL[i].setDamping(damping);
        combsR[i].setDamping(damping);
    }
}

void DragonflyReverb::RoomReverb::setSize(float size)
{
    // Size affects the delay times
    float scaleFactor = 0.5f + size * 1.5f;

    for (int i = 0; i < numCombs; ++i)
    {
        int sizeL = static_cast<int>(combTuningsMs[i] * scaleFactor * sampleRate * 0.001f);
        int sizeR = static_cast<int>(combTuningsMs[i] * scaleFactor * sampleRate * 0.001f * 1.01f);

        combsL[i].setSize(sizeL);
        combsR[i].setSize(sizeR);
    }
}

void DragonflyReverb::RoomReverb::process(const float* inputL, const float* inputR,
                                          float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float inL = inputL[i];
        float inR = inputR[i];

        // Mix input to mono and scale
        float input = (inL + inR) * 0.015f;

        // Process through parallel comb filters
        float combSumL = 0.0f;
        float combSumR = 0.0f;

        for (int c = 0; c < numCombs; ++c)
        {
            combSumL += combsL[c].process(input);
            combSumR += combsR[c].process(input);
        }

        // Scale comb output
        combSumL *= 0.25f;
        combSumR *= 0.25f;

        // Process through series allpass filters
        float outL = combSumL;
        float outR = combSumR;

        for (int a = 0; a < numAllpasses; ++a)
        {
            outL = allpassesL[a].process(outL);
            outR = allpassesR[a].process(outR);
        }

        outputL[i] = outL;
        outputR[i] = outR;
    }
}

void DragonflyReverb::RoomReverb::clear()
{
    for (int i = 0; i < numCombs; ++i)
    {
        combsL[i].clear();
        combsR[i].clear();
    }

    for (int i = 0; i < numAllpasses; ++i)
    {
        allpassesL[i].clear();
        allpassesR[i].clear();
    }
}

//==============================================================================
// HallReverb (Zrev2-style) implementation
void DragonflyReverb::HallReverb::prepare(double sr)
{
    sampleRate = sr;

    // Set up input diffusion
    float diffusionTimes[] = {4.771f, 3.595f, 2.734f, 1.987f};
    for (int i = 0; i < 4; ++i)
    {
        int size = static_cast<int>(diffusionTimes[i] * sampleRate * 0.001f);
        inputDiffusion[i].setSize(size);
        inputDiffusion[i].setFeedback(0.75f);
    }

    // Set up comb filters for hall
    for (int i = 0; i < numCombs; ++i)
    {
        int sizeL = static_cast<int>(combTuningsMs[i] * sampleRate * 0.001f);
        int sizeR = static_cast<int>(combTuningsMs[i] * sampleRate * 0.001f * 1.01f);

        combsL[i].setSize(sizeL);
        combsR[i].setSize(sizeR);
        combsL[i].setFeedback(decayFeedback);
        combsR[i].setFeedback(decayFeedback);
        combsL[i].setDamping(damping);
        combsR[i].setDamping(damping);
    }

    // Set up allpass filters
    for (int i = 0; i < numAllpasses; ++i)
    {
        int sizeL = static_cast<int>(allpassTuningsMs[i] * sampleRate * 0.001f);
        int sizeR = static_cast<int>(allpassTuningsMs[i] * sampleRate * 0.001f * 1.01f);

        allpassesL[i].setSize(sizeL);
        allpassesR[i].setSize(sizeR);
        allpassesL[i].setFeedback(0.7f);
        allpassesR[i].setFeedback(0.7f);
    }

    clear();
}

void DragonflyReverb::HallReverb::setDecayTime(float seconds)
{
    decayFeedback = std::exp(-3.0f / (seconds * sampleRate / 1000.0f));
    decayFeedback = juce::jlimit(0.0f, 0.98f, decayFeedback);

    for (int i = 0; i < numCombs; ++i)
    {
        combsL[i].setFeedback(decayFeedback);
        combsR[i].setFeedback(decayFeedback);
    }
}

void DragonflyReverb::HallReverb::setDamping(float damp)
{
    damping = juce::jlimit(0.0f, 1.0f, damp);

    for (int i = 0; i < numCombs; ++i)
    {
        combsL[i].setDamping(damping);
        combsR[i].setDamping(damping);
    }
}

void DragonflyReverb::HallReverb::setDiffusion(float diff)
{
    diffusion = juce::jlimit(0.0f, 1.0f, diff);

    // Adjust input diffusion feedback
    for (int i = 0; i < 4; ++i)
    {
        inputDiffusion[i].setFeedback(0.5f + diffusion * 0.45f);
    }
}

void DragonflyReverb::HallReverb::setSize(float size)
{
    float scaleFactor = 0.5f + size * 1.5f;

    for (int i = 0; i < numCombs; ++i)
    {
        int sizeL = static_cast<int>(combTuningsMs[i] * scaleFactor * sampleRate * 0.001f);
        int sizeR = static_cast<int>(combTuningsMs[i] * scaleFactor * sampleRate * 0.001f * 1.01f);

        combsL[i].setSize(sizeL);
        combsR[i].setSize(sizeR);
    }
}

void DragonflyReverb::HallReverb::process(const float* inputL, const float* inputR,
                                          float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Mix to mono and apply input diffusion
        float input = (inputL[i] + inputR[i]) * 0.015f;

        for (int d = 0; d < 4; ++d)
        {
            input = inputDiffusion[d].process(input);
        }

        // Process through parallel comb filters
        float combSumL = 0.0f;
        float combSumR = 0.0f;

        for (int c = 0; c < numCombs; ++c)
        {
            combSumL += combsL[c].process(input);
            combSumR += combsR[c].process(input);
        }

        combSumL *= 0.2f;
        combSumR *= 0.2f;

        // Process through series allpass filters
        float outL = combSumL;
        float outR = combSumR;

        for (int a = 0; a < numAllpasses; ++a)
        {
            outL = allpassesL[a].process(outL);
            outR = allpassesR[a].process(outR);
        }

        outputL[i] = outL;
        outputR[i] = outR;
    }
}

void DragonflyReverb::HallReverb::clear()
{
    for (int i = 0; i < 4; ++i)
    {
        inputDiffusion[i].clear();
    }

    for (int i = 0; i < numCombs; ++i)
    {
        combsL[i].clear();
        combsR[i].clear();
    }

    for (int i = 0; i < numAllpasses; ++i)
    {
        allpassesL[i].clear();
        allpassesR[i].clear();
    }
}

//==============================================================================
// PlateReverb implementation
void DragonflyReverb::PlateReverb::prepare(double sr)
{
    sampleRate = sr;

    // Set up delays with plate-specific timings
    for (int i = 0; i < numDelays; ++i)
    {
        int size = static_cast<int>(delayTimesMs[i] * sampleRate * 0.001f);
        delaysL[i].setMaxSize(size);
        delaysR[i].setMaxSize(size * 1.01f);
    }

    // Set up diffusion allpasses
    float diffTimes[] = {4.31f, 3.73f, 2.89f, 2.13f};
    for (int i = 0; i < 4; ++i)
    {
        int size = static_cast<int>(diffTimes[i] * sampleRate * 0.001f);
        diffusionL[i].setSize(size);
        diffusionR[i].setSize(size * 1.01f);
        diffusionL[i].setFeedback(0.7f);
        diffusionR[i].setFeedback(0.7f);
    }

    clear();
}

void DragonflyReverb::PlateReverb::setDecayTime(float seconds)
{
    decay = std::exp(-3.0f / (seconds * sampleRate / 1000.0f));
    decay = juce::jlimit(0.0f, 0.99f, decay);
}

void DragonflyReverb::PlateReverb::setDamping(float damp)
{
    damping = juce::jlimit(0.0f, 1.0f, damp);
}

void DragonflyReverb::PlateReverb::process(const float* inputL, const float* inputR,
                                           float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float inL = inputL[i] * 0.015f;
        float inR = inputR[i] * 0.015f;

        // Apply input diffusion
        for (int d = 0; d < 4; ++d)
        {
            inL = diffusionL[d].process(inL);
            inR = diffusionR[d].process(inR);
        }

        // Process through delay network
        float sumL = 0.0f;
        float sumR = 0.0f;

        for (int d = 0; d < numDelays; ++d)
        {
            float delayedL = delaysL[d].read(delayTimesMs[d] * sampleRate * 0.001f);
            float delayedR = delaysR[d].read(delayTimesMs[d] * sampleRate * 0.001f * 1.01f);

            // Apply damping
            lpStatesL[d] = delayedL * (1.0f - damping) + lpStatesL[d] * damping;
            lpStatesR[d] = delayedR * (1.0f - damping) + lpStatesR[d] * damping;

            // Feedback with decay
            delaysL[d].write(inL + lpStatesL[d] * decay);
            delaysR[d].write(inR + lpStatesR[d] * decay);

            sumL += lpStatesL[d];
            sumR += lpStatesR[d];
        }

        outputL[i] = sumL * 0.25f;
        outputR[i] = sumR * 0.25f;
    }
}

void DragonflyReverb::PlateReverb::clear()
{
    for (int i = 0; i < numDelays; ++i)
    {
        delaysL[i].clear();
        delaysR[i].clear();
        lpStatesL[i] = 0.0f;
        lpStatesR[i] = 0.0f;
    }

    for (int i = 0; i < 4; ++i)
    {
        diffusionL[i].clear();
        diffusionR[i].clear();
    }
}

//==============================================================================
// Main DragonflyReverb implementation
DragonflyReverb::DragonflyReverb()
{
}

void DragonflyReverb::prepare(double sr, int samplesPerBlock)
{
    sampleRate = sr;

    // Prepare all reverb types
    earlyReflections.prepare(sampleRate);
    roomReverb.prepare(sampleRate);
    hallReverb.prepare(sampleRate);
    plateReverb.prepare(sampleRate);

    // Set up pre-delay
    int maxPreDelay = static_cast<int>(sampleRate * 0.2f); // 200ms max
    preDelayL.setMaxSize(maxPreDelay);
    preDelayR.setMaxSize(maxPreDelay);

    // Set up filters
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;

    inputHighpass.prepare(spec);
    inputLowpass.prepare(spec);
    outputHighpass.prepare(spec);
    outputLowpass.prepare(spec);

    inputHighpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    inputLowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    outputHighpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    outputLowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    // Set default filter frequencies
    setLowCut(20.0f);
    setHighCut(20000.0f);

    // Allocate buffers
    int bufferSize = samplesPerBlock;
    tempBufferL.resize(bufferSize);
    tempBufferR.resize(bufferSize);
    earlyBufferL.resize(bufferSize);
    earlyBufferR.resize(bufferSize);
    lateBufferL.resize(bufferSize);
    lateBufferR.resize(bufferSize);

    reset();
}

void DragonflyReverb::reset()
{
    earlyReflections.clear();
    roomReverb.clear();
    hallReverb.clear();
    plateReverb.clear();

    preDelayL.clear();
    preDelayR.clear();

    inputHighpass.reset();
    inputLowpass.reset();
    outputHighpass.reset();
    outputLowpass.reset();
}

void DragonflyReverb::setPreDelay(float ms)
{
    preDelayTime = juce::jlimit(0.0f, 200.0f, ms);
}

void DragonflyReverb::setSize(float size)
{
    size = juce::jlimit(0.0f, 1.0f, size);
    earlyReflections.setRoomSize(0.5f + size * 1.5f);
    roomReverb.setSize(size);
    hallReverb.setSize(size);
}

void DragonflyReverb::setDecayTime(float seconds)
{
    seconds = juce::jlimit(0.1f, 30.0f, seconds);
    roomReverb.setDecayTime(seconds);
    hallReverb.setDecayTime(seconds);
    plateReverb.setDecayTime(seconds);
}

void DragonflyReverb::setDamping(float amount)
{
    amount = juce::jlimit(0.0f, 1.0f, amount);
    roomReverb.setDamping(amount);
    hallReverb.setDamping(amount);
    plateReverb.setDamping(amount);
}

void DragonflyReverb::setDiffusion(float amount)
{
    amount = juce::jlimit(0.0f, 1.0f, amount);
    hallReverb.setDiffusion(amount);
}

void DragonflyReverb::setLowCut(float freq)
{
    inputHighpass.setCutoffFrequency(freq);
    outputHighpass.setCutoffFrequency(freq * 0.8f);
}

void DragonflyReverb::setHighCut(float freq)
{
    inputLowpass.setCutoffFrequency(freq);
    outputLowpass.setCutoffFrequency(freq * 1.2f);
}

void DragonflyReverb::processBlock(juce::AudioBuffer<float>& buffer)
{
    switch (currentType)
    {
        case ReverbType::Room:
            processRoom(buffer);
            break;
        case ReverbType::Hall:
            processHall(buffer);
            break;
        case ReverbType::Plate:
            processPlate(buffer);
            break;
        case ReverbType::EarlyReflections:
            processEarlyOnly(buffer);
            break;
    }
}

void DragonflyReverb::processRoom(juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inputL;
    float* outputL = buffer.getWritePointer(0);
    float* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;

    // Copy dry signal
    std::copy(inputL, inputL + numSamples, tempBufferL.data());
    std::copy(inputR, inputR + numSamples, tempBufferR.data());

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        if (preDelayTime > 0)
        {
            float delayedL = preDelayL.read(preDelayTime * sampleRate * 0.001f);
            float delayedR = preDelayR.read(preDelayTime * sampleRate * 0.001f);
            preDelayL.write(inputL[i]);
            preDelayR.write(inputR[i]);
            tempBufferL[i] = delayedL;
            tempBufferR[i] = delayedR;
        }
    }

    // Process early reflections
    earlyReflections.process(tempBufferL.data(), tempBufferR.data(),
                            earlyBufferL.data(), earlyBufferR.data(), numSamples);

    // Process late reverb
    roomReverb.process(tempBufferL.data(), tempBufferR.data(),
                      lateBufferL.data(), lateBufferR.data(), numSamples);

    // Mix dry, early, and late
    for (int i = 0; i < numSamples; ++i)
    {
        float wetL = earlyBufferL[i] * earlyMix + lateBufferL[i] * lateMix;
        float wetR = earlyBufferR[i] * earlyMix + lateBufferR[i] * lateMix;

        // Apply stereo width
        float mid = (wetL + wetR) * 0.5f;
        float side = (wetL - wetR) * 0.5f * width;
        wetL = mid + side;
        wetR = mid - side;

        outputL[i] = inputL[i] * dryMix + wetL * wetMix;
        outputR[i] = inputR[i] * dryMix + wetR * wetMix;
    }
}

void DragonflyReverb::processHall(juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inputL;
    float* outputL = buffer.getWritePointer(0);
    float* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;

    // Copy dry signal
    std::copy(inputL, inputL + numSamples, tempBufferL.data());
    std::copy(inputR, inputR + numSamples, tempBufferR.data());

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        if (preDelayTime > 0)
        {
            float delayedL = preDelayL.read(preDelayTime * sampleRate * 0.001f);
            float delayedR = preDelayR.read(preDelayTime * sampleRate * 0.001f);
            preDelayL.write(inputL[i]);
            preDelayR.write(inputR[i]);
            tempBufferL[i] = delayedL;
            tempBufferR[i] = delayedR;
        }
    }

    // Process early reflections
    earlyReflections.process(tempBufferL.data(), tempBufferR.data(),
                            earlyBufferL.data(), earlyBufferR.data(), numSamples);

    // Process late reverb
    hallReverb.process(tempBufferL.data(), tempBufferR.data(),
                      lateBufferL.data(), lateBufferR.data(), numSamples);

    // Mix dry, early, and late
    for (int i = 0; i < numSamples; ++i)
    {
        float wetL = earlyBufferL[i] * earlyMix + lateBufferL[i] * lateMix;
        float wetR = earlyBufferR[i] * earlyMix + lateBufferR[i] * lateMix;

        // Apply stereo width
        float mid = (wetL + wetR) * 0.5f;
        float side = (wetL - wetR) * 0.5f * width;
        wetL = mid + side;
        wetR = mid - side;

        outputL[i] = inputL[i] * dryMix + wetL * wetMix;
        outputR[i] = inputR[i] * dryMix + wetR * wetMix;
    }
}

void DragonflyReverb::processPlate(juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inputL;
    float* outputL = buffer.getWritePointer(0);
    float* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;

    // Copy dry signal
    std::copy(inputL, inputL + numSamples, tempBufferL.data());
    std::copy(inputR, inputR + numSamples, tempBufferR.data());

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        if (preDelayTime > 0)
        {
            float delayedL = preDelayL.read(preDelayTime * sampleRate * 0.001f);
            float delayedR = preDelayR.read(preDelayTime * sampleRate * 0.001f);
            preDelayL.write(inputL[i]);
            preDelayR.write(inputR[i]);
            tempBufferL[i] = delayedL;
            tempBufferR[i] = delayedR;
        }
    }

    // Process plate reverb (no separate early reflections for plate)
    plateReverb.process(tempBufferL.data(), tempBufferR.data(),
                       lateBufferL.data(), lateBufferR.data(), numSamples);

    // Mix dry and wet
    for (int i = 0; i < numSamples; ++i)
    {
        float wetL = lateBufferL[i];
        float wetR = lateBufferR[i];

        // Apply stereo width
        float mid = (wetL + wetR) * 0.5f;
        float side = (wetL - wetR) * 0.5f * width;
        wetL = mid + side;
        wetR = mid - side;

        outputL[i] = inputL[i] * dryMix + wetL * wetMix;
        outputR[i] = inputR[i] * dryMix + wetR * wetMix;
    }
}

void DragonflyReverb::processEarlyOnly(juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inputL;
    float* outputL = buffer.getWritePointer(0);
    float* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;

    // Process early reflections only
    earlyReflections.process(inputL, inputR,
                            earlyBufferL.data(), earlyBufferR.data(), numSamples);

    // Mix dry and early
    for (int i = 0; i < numSamples; ++i)
    {
        outputL[i] = inputL[i] * dryMix + earlyBufferL[i] * wetMix;
        outputR[i] = inputR[i] * dryMix + earlyBufferR[i] * wetMix;
    }
}
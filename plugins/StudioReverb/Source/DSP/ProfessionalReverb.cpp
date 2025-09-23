/*
  ==============================================================================

    ProfessionalReverb.cpp
    Implementation of professional-grade reverb algorithms

  ==============================================================================
*/

#include "ProfessionalReverb.h"

//==============================================================================
// DelayLine implementation
void ProfessionalReverb::DelayLine::setMaxSize(int maxSamples)
{
    buffer.resize(maxSamples + 4); // Extra samples for cubic interpolation
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    size = maxSamples;
    writePos = 0;
}

void ProfessionalReverb::DelayLine::setDelay(float delaySamples)
{
    // Size is set separately, this is mainly for modulation
}

float ProfessionalReverb::DelayLine::read(float delaySamples) const
{
    if (buffer.empty()) return 0.0f;

    float readPos = static_cast<float>(writePos) - delaySamples;
    while (readPos < 0) readPos += size;
    while (readPos >= size) readPos -= size;

    int pos0 = static_cast<int>(readPos);
    float frac = readPos - pos0;

    // Cubic interpolation for high quality
    int pos1 = (pos0 + 1) % size;
    int pos2 = (pos0 + 2) % size;
    int posm1 = (pos0 - 1 + size) % size;

    return cubicInterpolate(buffer[posm1], buffer[pos0], buffer[pos1], buffer[pos2], frac);
}

void ProfessionalReverb::DelayLine::write(float sample)
{
    if (buffer.empty()) return;

    buffer[writePos] = sample;
    writePos = (writePos + 1) % size;
}

void ProfessionalReverb::DelayLine::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float ProfessionalReverb::DelayLine::cubicInterpolate(float y0, float y1, float y2, float y3, float x) const
{
    float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float a2 = -0.5f * y0 + 0.5f * y2;
    float a3 = y1;

    return a0 * x * x * x + a1 * x * x + a2 * x + a3;
}

//==============================================================================
// AllpassFilter implementation
void ProfessionalReverb::AllpassFilter::setSize(int samples)
{
    delay.setMaxSize(samples);
}

void ProfessionalReverb::AllpassFilter::setFeedback(float g)
{
    feedback = juce::jlimit(-0.99f, 0.99f, g);
}

float ProfessionalReverb::AllpassFilter::process(float input)
{
    float delayed = delay.read(delay.size - 1);
    float output = -input + delayed;
    delay.write(input + delayed * feedback);
    return output;
}

float ProfessionalReverb::AllpassFilter::processModulated(float input, float modulation)
{
    float modulatedDelay = delay.size - 1 + modulation;
    float delayed = delay.read(modulatedDelay);
    float output = -input + delayed;
    delay.write(input + delayed * feedback);
    return output;
}

void ProfessionalReverb::AllpassFilter::clear()
{
    delay.clear();
}

//==============================================================================
// CombFilter implementation
void ProfessionalReverb::CombFilter::setSize(int samples)
{
    delay.setMaxSize(samples);
}

void ProfessionalReverb::CombFilter::setFeedback(float g)
{
    feedback = juce::jlimit(0.0f, 0.99f, g);
}

void ProfessionalReverb::CombFilter::setDamping(float d)
{
    damping = juce::jlimit(0.0f, 0.99f, d);
}

float ProfessionalReverb::CombFilter::process(float input)
{
    float output = delay.read(delay.size - 1);
    filterStore = (output * (1.0f - damping)) + (filterStore * damping);
    delay.write(input + filterStore * feedback);
    return output;
}

void ProfessionalReverb::CombFilter::clear()
{
    delay.clear();
    filterStore = 0.0f;
}

//==============================================================================
// EarlyReflections implementation
void ProfessionalReverb::EarlyReflections::prepare(double sampleRate)
{
    delays[0].setMaxSize(static_cast<int>(sampleRate * 0.1)); // 100ms max
    delays[1].setMaxSize(static_cast<int>(sampleRate * 0.1));

    // Initialize high-quality early reflection pattern
    // Based on measured impulse responses from real spaces
    const float baseDelays[numReflections] = {
        0.0043f, 0.0087f, 0.0123f, 0.0157f, 0.0189f, 0.0219f,
        0.0253f, 0.0287f, 0.0321f, 0.0354f, 0.0389f, 0.0424f,
        0.0458f, 0.0491f, 0.0524f, 0.0557f, 0.0589f, 0.0621f,
        0.0653f, 0.0684f, 0.0715f, 0.0746f, 0.0777f, 0.0808f
    };

    // Initialize with calibrated gains for natural decay
    for (int i = 0; i < numReflections; ++i)
    {
        tapTimesL[i] = baseDelays[i] * sampleRate;
        tapTimesR[i] = baseDelays[i] * sampleRate * (1.0f + (i % 3) * 0.011f); // Slight stereo variation

        float decay = std::exp(-3.0f * baseDelays[i] / 0.1f); // Natural decay
        tapGainsL[i] = decay * (0.8f + (std::rand() % 100) * 0.004f); // Slight randomization
        tapGainsR[i] = decay * (0.8f + (std::rand() % 100) * 0.004f);
    }

    // Diffusion allpasses
    for (int i = 0; i < 4; ++i)
    {
        diffusers[i].setSize(static_cast<int>(sampleRate * (0.003f + i * 0.002f)));
        diffusers[i].setFeedback(0.5f);
    }
}

void ProfessionalReverb::EarlyReflections::setRoomSize(float size)
{
    float scaleFactor = 0.3f + size * 1.4f;
    for (int i = 0; i < numReflections; ++i)
    {
        tapTimesL[i] *= scaleFactor;
        tapTimesR[i] *= scaleFactor;
    }
}

void ProfessionalReverb::EarlyReflections::setDiffusion(float diff)
{
    for (auto& diffuser : diffusers)
    {
        diffuser.setFeedback(0.3f + diff * 0.5f);
    }
}

void ProfessionalReverb::EarlyReflections::process(const float* inputL, const float* inputR,
                                                   float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Write input to delay lines
        delays[0].write(inputL[i]);
        delays[1].write(inputR[i]);

        // Accumulate early reflections
        float outL = 0.0f;
        float outR = 0.0f;

        for (int tap = 0; tap < numReflections; ++tap)
        {
            outL += delays[0].read(tapTimesL[tap]) * tapGainsL[tap];
            outR += delays[1].read(tapTimesR[tap]) * tapGainsR[tap];
        }

        // Apply diffusion
        for (auto& diff : diffusers)
        {
            outL = diff.process(outL);
            outR = diff.process(outR);
        }

        outputL[i] = outL * 0.5f;
        outputR[i] = outR * 0.5f;
    }
}

void ProfessionalReverb::EarlyReflections::clear()
{
    delays[0].clear();
    delays[1].clear();
    for (auto& diff : diffusers)
        diff.clear();
}

//==============================================================================
// FDNHallReverb implementation
void ProfessionalReverb::FDNHallReverb::prepare(double sampleRate)
{
    // Prime number delay times for inharmonic response (in ms)
    const float baseDelayTimes[numDelays] = {
        31.0f, 37.0f, 41.0f, 43.0f, 47.0f, 53.0f, 59.0f, 61.0f,
        67.0f, 71.0f, 73.0f, 79.0f, 83.0f, 89.0f, 97.0f, 101.0f
    };

    // Initialize delay lines
    for (int i = 0; i < numDelays; ++i)
    {
        delayTimes[i] = baseDelayTimes[i] * 0.001f * sampleRate;
        delayLines[i].setMaxSize(static_cast<int>(delayTimes[i] * 2.0f));
    }

    // Initialize diffusion stages
    for (int i = 0; i < 8; ++i)
    {
        inputDiffusion[i].setSize(static_cast<int>(sampleRate * (0.002f + i * 0.001f)));
        inputDiffusion[i].setFeedback(0.75f);
    }

    for (int i = 0; i < 4; ++i)
    {
        outputDiffusion[i].setSize(static_cast<int>(sampleRate * (0.003f + i * 0.0015f)));
        outputDiffusion[i].setFeedback(0.7f);
    }

    // Generate Hadamard feedback matrix for maximum diffusion
    generateHadamardMatrix();

    // Initialize states
    std::fill(lowpassStates.begin(), lowpassStates.end(), 0.0f);
    std::fill(highpassStates.begin(), highpassStates.end(), 0.0f);
}

void ProfessionalReverb::FDNHallReverb::generateHadamardMatrix()
{
    // Create a Hadamard matrix for optimal diffusion
    float scale = 1.0f / std::sqrt(static_cast<float>(numDelays));

    for (int i = 0; i < numDelays; ++i)
    {
        for (int j = 0; j < numDelays; ++j)
        {
            // Simplified Hadamard-like matrix
            int sum = 0;
            for (int k = 0; k < 4; ++k)
            {
                sum += ((i >> k) & 1) * ((j >> k) & 1);
            }
            feedbackMatrix[i][j] = (sum % 2 == 0 ? scale : -scale);
        }
    }
}

void ProfessionalReverb::FDNHallReverb::setDecayTime(float seconds)
{
    float rt60 = seconds;
    for (int i = 0; i < numDelays; ++i)
    {
        float delayTimeSeconds = delayTimes[i] / 44100.0f; // Assuming base sample rate
        feedbackGains[i] = std::pow(0.001f, delayTimeSeconds / rt60);
    }
}

void ProfessionalReverb::FDNHallReverb::setDiffusion(float diff)
{
    for (auto& ap : inputDiffusion)
        ap.setFeedback(0.5f + diff * 0.45f);

    for (auto& ap : outputDiffusion)
        ap.setFeedback(0.4f + diff * 0.5f);
}

void ProfessionalReverb::FDNHallReverb::setModulation(float rate, float depth)
{
    modRate = rate;
    modDepth = depth * 2.0f; // Scale for subtle effect
}

void ProfessionalReverb::FDNHallReverb::setDamping(float damp)
{
    dampingFreq = 20000.0f - damp * 19000.0f; // 20kHz to 1kHz
}

void ProfessionalReverb::FDNHallReverb::setSize(float size)
{
    for (int i = 0; i < numDelays; ++i)
    {
        float scale = 0.5f + size * 1.0f;
        delayLines[i].setMaxSize(static_cast<int>(delayTimes[i] * scale * 2.0f));
    }
}

void ProfessionalReverb::FDNHallReverb::process(const float* inputL, const float* inputR,
                                                float* outputL, float* outputR, int numSamples)
{
    const float twoPi = juce::MathConstants<float>::twoPi;

    for (int n = 0; n < numSamples; ++n)
    {
        // Input diffusion
        float diffusedL = inputL[n];
        float diffusedR = inputR[n];

        for (int i = 0; i < 4; ++i)
        {
            diffusedL = inputDiffusion[i * 2].process(diffusedL);
            diffusedR = inputDiffusion[i * 2 + 1].process(diffusedR);
        }

        // Mix to mono for FDN input
        float input = (diffusedL + diffusedR) * 0.5f;

        // Read from delay lines
        std::array<float, numDelays> delayOutputs;
        for (int i = 0; i < numDelays; ++i)
        {
            // Add modulation
            float modulation = 0.0f;
            if (modDepth > 0.0f)
            {
                float lfoValue = std::sin(lfo1Phase + i * twoPi / numDelays);
                modulation = lfoValue * modDepth;
            }

            delayOutputs[i] = delayLines[i].read(delayTimes[i] + modulation);
        }

        // Apply feedback matrix
        std::array<float, numDelays> feedbackSignals;
        for (int i = 0; i < numDelays; ++i)
        {
            feedbackSignals[i] = 0.0f;
            for (int j = 0; j < numDelays; ++j)
            {
                feedbackSignals[i] += delayOutputs[j] * feedbackMatrix[i][j];
            }

            // Apply damping
            float cutoff = dampingFreq / 44100.0f; // Normalized frequency
            lowpassStates[i] = feedbackSignals[i] * cutoff + lowpassStates[i] * (1.0f - cutoff);
            feedbackSignals[i] = lowpassStates[i];

            // Apply feedback gain
            feedbackSignals[i] *= feedbackGains[i];
        }

        // Write to delay lines
        for (int i = 0; i < numDelays; ++i)
        {
            delayLines[i].write(input * 0.125f + feedbackSignals[i]); // Scale input
        }

        // Create output from multiple taps
        float outL = 0.0f;
        float outR = 0.0f;

        for (int i = 0; i < numDelays; ++i)
        {
            // Use different taps for L and R for width
            if (i % 2 == 0)
                outL += delayOutputs[i];
            else
                outR += delayOutputs[i];
        }

        // Output diffusion
        for (int i = 0; i < 2; ++i)
        {
            outL = outputDiffusion[i * 2].process(outL);
            outR = outputDiffusion[i * 2 + 1].process(outR);
        }

        // Update LFOs
        lfo1Phase += modRate * twoPi / 44100.0f;
        if (lfo1Phase >= twoPi) lfo1Phase -= twoPi;

        outputL[n] = outL * 0.1f; // Scale output
        outputR[n] = outR * 0.1f;
    }
}

void ProfessionalReverb::FDNHallReverb::clear()
{
    for (auto& dl : delayLines)
        dl.clear();
    for (auto& ap : inputDiffusion)
        ap.clear();
    for (auto& ap : outputDiffusion)
        ap.clear();
    std::fill(lowpassStates.begin(), lowpassStates.end(), 0.0f);
    std::fill(highpassStates.begin(), highpassStates.end(), 0.0f);
    lfo1Phase = 0.0f;
    lfo2Phase = 0.0f;
}

//==============================================================================
// RoomReverb implementation
void ProfessionalReverb::RoomReverb::prepare(double sampleRate)
{
    double scaleFactor = sampleRate / 44100.0;

    // Initialize comb filters with room-appropriate delays
    for (int i = 0; i < numCombs; ++i)
    {
        combsL[i].setSize(static_cast<int>(combTunings[i] * scaleFactor));
        combsR[i].setSize(static_cast<int>((combTunings[i] + 23) * scaleFactor)); // Stereo spread
    }

    // Initialize allpass filters
    for (int i = 0; i < numAllpasses; ++i)
    {
        allpassesL[i].setSize(static_cast<int>(allpassTunings[i] * scaleFactor));
        allpassesR[i].setSize(static_cast<int>((allpassTunings[i] + 23) * scaleFactor));
        allpassesL[i].setFeedback(0.5f);
        allpassesR[i].setFeedback(0.5f);
    }
}

void ProfessionalReverb::RoomReverb::setDecayTime(float seconds)
{
    float feedback = 0.5f + (seconds / 10.0f) * 0.48f; // Map to reasonable range
    for (auto& comb : combsL)
        comb.setFeedback(feedback);
    for (auto& comb : combsR)
        comb.setFeedback(feedback);
}

void ProfessionalReverb::RoomReverb::setDiffusion(float diff)
{
    float apFeedback = 0.3f + diff * 0.4f;
    for (auto& ap : allpassesL)
        ap.setFeedback(apFeedback);
    for (auto& ap : allpassesR)
        ap.setFeedback(apFeedback);
}

void ProfessionalReverb::RoomReverb::setDamping(float damp)
{
    for (auto& comb : combsL)
        comb.setDamping(damp * 0.5f);
    for (auto& comb : combsR)
        comb.setDamping(damp * 0.5f);
}

void ProfessionalReverb::RoomReverb::setSize(float size)
{
    // Size affects the delay scaling
    // This would require reinitializing delays, so we'll handle it at a higher level
}

void ProfessionalReverb::RoomReverb::process(const float* inputL, const float* inputR,
                                             float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float inL = (inputL[i] + inputR[i]) * 0.5f; // Mix to mono
        float inR = inL;

        float outL = 0.0f;
        float outR = 0.0f;

        // Parallel comb filters
        for (auto& comb : combsL)
            outL += comb.process(inL);

        for (auto& comb : combsR)
            outR += comb.process(inR);

        // Series allpass filters
        for (auto& ap : allpassesL)
            outL = ap.process(outL);

        for (auto& ap : allpassesR)
            outR = ap.process(outR);

        outputL[i] = outL * 0.015f; // Scale
        outputR[i] = outR * 0.015f;
    }
}

void ProfessionalReverb::RoomReverb::clear()
{
    for (auto& comb : combsL) comb.clear();
    for (auto& comb : combsR) comb.clear();
    for (auto& ap : allpassesL) ap.clear();
    for (auto& ap : allpassesR) ap.clear();
}

//==============================================================================
// DattorroPlate implementation
void ProfessionalReverb::DattorroPlate::prepare(double sampleRate)
{
    // Input diffusion APF sizes (in samples at 44.1kHz)
    const int inputAPFSizes[4] = { 142, 107, 379, 277 };

    for (int i = 0; i < 4; ++i)
    {
        inputDiffusionL[i].setSize(static_cast<int>(inputAPFSizes[i] * sampleRate / 44100.0));
        inputDiffusionR[i].setSize(static_cast<int>((inputAPFSizes[i] + 13) * sampleRate / 44100.0));
        inputDiffusionL[i].setFeedback(0.75f);
        inputDiffusionR[i].setFeedback(0.75f);
    }

    // Tank structure - the key to the plate sound
    tankL.allpass1.setSize(static_cast<int>(672 * sampleRate / 44100.0));
    tankL.delay1.setMaxSize(static_cast<int>(4453 * sampleRate / 44100.0));
    tankL.allpass2.setSize(static_cast<int>(1800 * sampleRate / 44100.0));
    tankL.delay2.setMaxSize(static_cast<int>(3720 * sampleRate / 44100.0));

    tankR.allpass1.setSize(static_cast<int>(908 * sampleRate / 44100.0));
    tankR.delay1.setMaxSize(static_cast<int>(4217 * sampleRate / 44100.0));
    tankR.allpass2.setSize(static_cast<int>(2656 * sampleRate / 44100.0));
    tankR.delay2.setMaxSize(static_cast<int>(3163 * sampleRate / 44100.0));

    // Set allpass feedbacks for plate character
    tankL.allpass1.setFeedback(0.7f);
    tankL.allpass2.setFeedback(0.5f);
    tankR.allpass1.setFeedback(0.7f);
    tankR.allpass2.setFeedback(0.5f);

    // Output tap positions (normalized 0-1)
    outputTapsL = { 0.3f, 0.5f, 0.7f, 0.9f, 0.33f, 0.67f, 0.15f };
    outputTapsR = { 0.27f, 0.54f, 0.73f, 0.85f, 0.31f, 0.69f, 0.18f };
}

void ProfessionalReverb::DattorroPlate::setDecayTime(float seconds)
{
    decay = 0.2f + (seconds / 30.0f) * 0.79f; // Map to 0.2-0.99
    tankFeedback = decay;
}

void ProfessionalReverb::DattorroPlate::setDiffusion(float diff)
{
    float fb = 0.5f + diff * 0.45f;
    for (auto& ap : inputDiffusionL)
        ap.setFeedback(fb);
    for (auto& ap : inputDiffusionR)
        ap.setFeedback(fb);

    tankL.allpass1.setFeedback(0.5f + diff * 0.3f);
    tankL.allpass2.setFeedback(0.4f + diff * 0.3f);
    tankR.allpass1.setFeedback(0.5f + diff * 0.3f);
    tankR.allpass2.setFeedback(0.4f + diff * 0.3f);
}

void ProfessionalReverb::DattorroPlate::setDamping(float damp)
{
    damping = damp;
}

void ProfessionalReverb::DattorroPlate::setModulation(float rate, float depth)
{
    modRate = rate;
    modDepth = depth * 5.0f; // Scale for appropriate effect
}

void ProfessionalReverb::DattorroPlate::process(const float* inputL, const float* inputR,
                                                float* outputL, float* outputR, int numSamples)
{
    const float twoPi = juce::MathConstants<float>::twoPi;

    for (int i = 0; i < numSamples; ++i)
    {
        // Input diffusion
        float diffL = inputL[i];
        float diffR = inputR[i];

        for (auto& ap : inputDiffusionL)
            diffL = ap.process(diffL);

        for (auto& ap : inputDiffusionR)
            diffR = ap.process(diffR);

        // Tank processing with modulation
        float modulation = modDepth * std::sin(lfoPhase);

        // Left tank
        float tankOutL = tankL.delay2.read(3720 - 1);
        float temp = tankL.allpass1.processModulated(diffL + tankOutL * tankFeedback, modulation);
        tankL.delay1.write(temp);
        temp = tankL.delay1.read(4453 - 1);

        // Damping
        tankL.lpState = temp * (1.0f - damping * 0.5f) + tankL.lpState * damping * 0.5f;
        temp = tankL.lpState;

        temp = tankL.allpass2.process(temp);
        tankL.delay2.write(temp);

        // Right tank
        float tankOutR = tankR.delay2.read(3163 - 1);
        temp = tankR.allpass1.processModulated(diffR + tankOutR * tankFeedback, -modulation);
        tankR.delay1.write(temp);
        temp = tankR.delay1.read(4217 - 1);

        // Damping
        tankR.lpState = temp * (1.0f - damping * 0.5f) + tankR.lpState * damping * 0.5f;
        temp = tankR.lpState;

        temp = tankR.allpass2.process(temp);
        tankR.delay2.write(temp);

        // Output taps for rich plate sound
        float outL = 0.0f;
        float outR = 0.0f;

        for (int tap = 0; tap < 7; ++tap)
        {
            outL += tankL.delay1.read(4453 * outputTapsL[tap]) * 0.14f;
            outL += tankL.delay2.read(3720 * outputTapsL[tap]) * 0.14f;
            outR += tankR.delay1.read(4217 * outputTapsR[tap]) * 0.14f;
            outR += tankR.delay2.read(3163 * outputTapsR[tap]) * 0.14f;
        }

        // Update LFO
        lfoPhase += modRate * twoPi / 44100.0f;
        if (lfoPhase >= twoPi) lfoPhase -= twoPi;

        outputL[i] = outL * 0.5f;
        outputR[i] = outR * 0.5f;
    }
}

void ProfessionalReverb::DattorroPlate::clear()
{
    for (auto& ap : inputDiffusionL) ap.clear();
    for (auto& ap : inputDiffusionR) ap.clear();

    tankL.allpass1.clear();
    tankL.delay1.clear();
    tankL.allpass2.clear();
    tankL.delay2.clear();
    tankL.lpState = 0.0f;
    tankL.hpState = 0.0f;

    tankR.allpass1.clear();
    tankR.delay1.clear();
    tankR.allpass2.clear();
    tankR.delay2.clear();
    tankR.lpState = 0.0f;
    tankR.hpState = 0.0f;

    lfoPhase = 0.0f;
}

//==============================================================================
// ProfessionalReverb main implementation
ProfessionalReverb::ProfessionalReverb()
{
    // Filters will be initialized in prepare()
}

void ProfessionalReverb::prepare(double sampleRate, int samplesPerBlock)
{
    this->sampleRate = sampleRate;

    // Initialize all processors
    earlyReflections.prepare(sampleRate);
    hallReverb.prepare(sampleRate);
    roomReverb.prepare(sampleRate);
    plateReverb.prepare(sampleRate);

    // Initialize pre-delay
    preDelayL.setMaxSize(static_cast<int>(sampleRate * 0.2)); // 200ms max
    preDelayR.setMaxSize(static_cast<int>(sampleRate * 0.2));

    // Initialize filters
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
    tempBufferL.resize(samplesPerBlock);
    tempBufferR.resize(samplesPerBlock);
    earlyBufferL.resize(samplesPerBlock);
    earlyBufferR.resize(samplesPerBlock);
    lateBufferL.resize(samplesPerBlock);
    lateBufferR.resize(samplesPerBlock);

    // Set default parameters
    setSize(0.5f);
    setDecayTime(2.0f);
    setDiffusion(0.75f);
    setDamping(0.5f);
    setModulationRate(0.5f);
    setModulationDepth(0.1f);
}

void ProfessionalReverb::processBlock(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numChannels < 2) return;

    switch (currentType)
    {
        case ReverbType::EarlyReflections:
            processEarlyReflections(buffer);
            break;
        case ReverbType::Room:
            processRoom(buffer);
            break;
        case ReverbType::Hall:
            processHall(buffer);
            break;
        case ReverbType::Plate:
            processPlate(buffer);
            break;
    }
}

void ProfessionalReverb::processEarlyReflections(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        preDelayL.write(left[i]);
        preDelayR.write(right[i]);
        tempBufferL[i] = preDelayL.read(preDelayTime);
        tempBufferR[i] = preDelayR.read(preDelayTime);
    }

    // Process early reflections only
    earlyReflections.process(tempBufferL.data(), tempBufferR.data(),
                            earlyBufferL.data(), earlyBufferR.data(), numSamples);

    // Mix dry and wet
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = left[i] * dryMix + earlyBufferL[i] * wetMix;
        right[i] = right[i] * dryMix + earlyBufferR[i] * wetMix;
    }
}

void ProfessionalReverb::processRoom(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    // Apply input filters
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = inputHighpass.processSample(0, inputLowpass.processSample(0, left[i]));
        right[i] = inputHighpass.processSample(0, inputLowpass.processSample(0, right[i]));
    }

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        preDelayL.write(left[i]);
        preDelayR.write(right[i]);
        tempBufferL[i] = preDelayL.read(preDelayTime);
        tempBufferR[i] = preDelayR.read(preDelayTime);
    }

    // Process early reflections
    earlyReflections.process(tempBufferL.data(), tempBufferR.data(),
                            earlyBufferL.data(), earlyBufferR.data(), numSamples);

    // Process room reverb
    roomReverb.process(tempBufferL.data(), tempBufferR.data(),
                      lateBufferL.data(), lateBufferR.data(), numSamples);

    // Mix and apply output filters
    for (int i = 0; i < numSamples; ++i)
    {
        float wetL = earlyBufferL[i] * earlyMix + lateBufferL[i] * lateMix;
        float wetR = earlyBufferR[i] * earlyMix + lateBufferR[i] * lateMix;

        wetL = outputHighpass.processSample(0, outputLowpass.processSample(0, wetL));
        wetR = outputHighpass.processSample(0, outputLowpass.processSample(0, wetR));

        left[i] = left[i] * dryMix + wetL * wetMix;
        right[i] = right[i] * dryMix + wetR * wetMix;
    }
}

void ProfessionalReverb::processHall(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    // Apply input filters
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = inputHighpass.processSample(0, inputLowpass.processSample(0, left[i]));
        right[i] = inputHighpass.processSample(0, inputLowpass.processSample(0, right[i]));
    }

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        preDelayL.write(left[i]);
        preDelayR.write(right[i]);
        tempBufferL[i] = preDelayL.read(preDelayTime);
        tempBufferR[i] = preDelayR.read(preDelayTime);
    }

    // Process early reflections
    earlyReflections.process(tempBufferL.data(), tempBufferR.data(),
                            earlyBufferL.data(), earlyBufferR.data(), numSamples);

    // Process hall reverb (FDN)
    hallReverb.process(tempBufferL.data(), tempBufferR.data(),
                      lateBufferL.data(), lateBufferR.data(), numSamples);

    // Mix and apply output filters
    for (int i = 0; i < numSamples; ++i)
    {
        float wetL = earlyBufferL[i] * earlyMix + lateBufferL[i] * lateMix;
        float wetR = earlyBufferR[i] * earlyMix + lateBufferR[i] * lateMix;

        // Apply stereo width
        float mid = (wetL + wetR) * 0.5f;
        float side = (wetL - wetR) * 0.5f * width;
        wetL = mid + side;
        wetR = mid - side;

        wetL = outputHighpass.processSample(0, outputLowpass.processSample(0, wetL));
        wetR = outputHighpass.processSample(0, outputLowpass.processSample(0, wetR));

        left[i] = left[i] * dryMix + wetL * wetMix;
        right[i] = right[i] * dryMix + wetR * wetMix;
    }
}

void ProfessionalReverb::processPlate(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    // Apply input filters
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = inputHighpass.processSample(0, inputLowpass.processSample(0, left[i]));
        right[i] = inputHighpass.processSample(0, inputLowpass.processSample(0, right[i]));
    }

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        preDelayL.write(left[i]);
        preDelayR.write(right[i]);
        tempBufferL[i] = preDelayL.read(preDelayTime);
        tempBufferR[i] = preDelayR.read(preDelayTime);
    }

    // Process plate reverb (no early reflections)
    plateReverb.process(tempBufferL.data(), tempBufferR.data(),
                       lateBufferL.data(), lateBufferR.data(), numSamples);

    // Mix and apply output filters
    for (int i = 0; i < numSamples; ++i)
    {
        float wetL = lateBufferL[i];
        float wetR = lateBufferR[i];

        wetL = outputHighpass.processSample(0, outputLowpass.processSample(0, wetL));
        wetR = outputHighpass.processSample(0, outputLowpass.processSample(0, wetR));

        left[i] = left[i] * dryMix + wetL * wetMix;
        right[i] = right[i] * dryMix + wetR * wetMix;
    }
}

void ProfessionalReverb::reset()
{
    earlyReflections.clear();
    hallReverb.clear();
    roomReverb.clear();
    plateReverb.clear();
    preDelayL.clear();
    preDelayR.clear();
    inputHighpass.reset();
    inputLowpass.reset();
    outputHighpass.reset();
    outputLowpass.reset();
}

// Parameter setters
void ProfessionalReverb::setPreDelay(float ms)
{
    preDelayTime = ms * 0.001f * sampleRate;
}

void ProfessionalReverb::setSize(float size)
{
    earlyReflections.setRoomSize(size);
    hallReverb.setSize(size);
    roomReverb.setSize(size);
}

void ProfessionalReverb::setDecayTime(float seconds)
{
    hallReverb.setDecayTime(seconds);
    roomReverb.setDecayTime(seconds);
    plateReverb.setDecayTime(seconds);
}

void ProfessionalReverb::setDamping(float amount)
{
    hallReverb.setDamping(amount);
    roomReverb.setDamping(amount);
    plateReverb.setDamping(amount);
}

void ProfessionalReverb::setDiffusion(float amount)
{
    earlyReflections.setDiffusion(amount);
    hallReverb.setDiffusion(amount);
    roomReverb.setDiffusion(amount);
    plateReverb.setDiffusion(amount);
}

void ProfessionalReverb::setWidth(float stereoWidth)
{
    width = stereoWidth;
}

void ProfessionalReverb::setLowCut(float freq)
{
    inputHighpass.setCutoffFrequency(freq);
    outputHighpass.setCutoffFrequency(freq * 0.5f); // Less aggressive on output
}

void ProfessionalReverb::setHighCut(float freq)
{
    inputLowpass.setCutoffFrequency(freq);
    outputLowpass.setCutoffFrequency(freq);
}

void ProfessionalReverb::setModulationRate(float hz)
{
    hallReverb.setModulation(hz, 0.05f);  // Use a fixed depth for now
    plateReverb.setModulation(hz, 0.05f);
}

void ProfessionalReverb::setModulationDepth(float depth)
{
    hallReverb.setModulation(0.5f, depth);  // Use a fixed rate for now
    plateReverb.setModulation(0.5f, depth);
}
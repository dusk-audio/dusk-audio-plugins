#include "PlateReverb.h"
#include <cmath>

PlateReverbProcessor::PlateReverbProcessor() = default;
PlateReverbProcessor::~PlateReverbProcessor() = default;

void PlateReverbProcessor::prepare(double sr, int bs)
{
    sampleRate = sr;
    blockSize = bs;

    float srScale = static_cast<float>(sampleRate) / 44100.0f;

    // Initialize input diffusion
    inputDiffusionL[0].setDelay(static_cast<int>(142 * srScale));
    inputDiffusionL[1].setDelay(static_cast<int>(379 * srScale));
    inputDiffusionR[0].setDelay(static_cast<int>(151 * srScale));
    inputDiffusionR[1].setDelay(static_cast<int>(389 * srScale));

    for (auto& diff : inputDiffusionL) diff.setFeedback(0.7f);
    for (auto& diff : inputDiffusionR) diff.setFeedback(0.7f);

    // Initialize plate network
    for (int plate = 0; plate < NUM_PARALLEL_PLATES; ++plate)
    {
        for (int stage = 0; stage < NUM_LATTICE_STAGES; ++stage)
        {
            int delayTime = static_cast<int>(LATTICE_DELAYS[stage] * srScale);
            plateNetworkL[plate][stage].setDelay(delayTime + plate * 17);
            plateNetworkR[plate][stage].setDelay(delayTime + plate * 19);
            plateNetworkL[plate][stage].setFeedback(0.5f);
            plateNetworkR[plate][stage].setFeedback(0.5f);
        }

        dampingFiltersL[plate].setCutoff(10000.0f, sampleRate);
        dampingFiltersR[plate].setCutoff(10000.0f, sampleRate);
    }

    // Initialize modulation phase offsets
    for (int i = 0; i < NUM_LATTICE_STAGES; ++i)
        modPhaseOffsets[i] = (i * 2.0f * juce::MathConstants<float>::pi) / NUM_LATTICE_STAGES;

    updateFilters();
    reset();
}

void PlateReverbProcessor::reset()
{
    for (auto& diff : inputDiffusionL) diff.clear();
    for (auto& diff : inputDiffusionR) diff.clear();

    for (auto& plate : plateNetworkL)
        for (auto& stage : plate) stage.clear();
    for (auto& plate : plateNetworkR)
        for (auto& stage : plate) stage.clear();

    for (auto& filter : dampingFiltersL) filter.clear();
    for (auto& filter : dampingFiltersR) filter.clear();

    preDelayL.clear();
    preDelayR.clear();

    inputHighpassL.reset();
    inputHighpassR.reset();
    outputLowpassL.reset();
    outputLowpassR.reset();

    modPhase = 0.0f;
}

void PlateReverbProcessor::process(float* leftChannel, float* rightChannel, int numSamples)
{
    updateFilters();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Apply pre-delay
        float inputL = preDelayL.process(leftChannel[sample]);
        float inputR = preDelayR.process(rightChannel[sample]);

        // Apply input filtering
        inputL = inputHighpassL.processSample(inputL);
        inputR = inputHighpassR.processSample(inputR);

        // Input diffusion
        for (auto& diff : inputDiffusionL)
            inputL = diff.process(inputL);
        for (auto& diff : inputDiffusionR)
            inputR = diff.process(inputR);

        // Process through parallel plate networks
        float outputL = 0.0f;
        float outputR = 0.0f;

        for (int plate = 0; plate < NUM_PARALLEL_PLATES; ++plate)
        {
            float plateL = inputL;
            float plateR = inputR;

            // Process through lattice stages with modulation
            for (int stage = 0; stage < NUM_LATTICE_STAGES; ++stage)
            {
                if (modulation > 0.0f)
                {
                    float modAmount = std::sin(modPhase + modPhaseOffsets[stage]) * modulation * 0.002f;
                    plateNetworkL[plate][stage].modulate(modAmount);
                    plateNetworkR[plate][stage].modulate(modAmount);
                }

                plateL = plateNetworkL[plate][stage].process(plateL);
                plateR = plateNetworkR[plate][stage].process(plateR);
            }

            // Apply damping
            plateL = dampingFiltersL[plate].process(plateL);
            plateR = dampingFiltersR[plate].process(plateR);

            outputL += plateL;
            outputR += plateR;
        }

        // Update modulation
        updateModulation();

        // Scale output
        outputL /= static_cast<float>(NUM_PARALLEL_PLATES);
        outputR /= static_cast<float>(NUM_PARALLEL_PLATES);

        // Apply output filtering
        outputL = outputLowpassL.processSample(outputL);
        outputR = outputLowpassR.processSample(outputR);

        leftChannel[sample] = outputL;
        rightChannel[sample] = outputR;
    }
}

void PlateReverbProcessor::updateFilters()
{
    // Update pre-delay
    int preDelaySamples = static_cast<int>(preDelay * sampleRate / 1000.0f);
    preDelayL.setDelay(preDelaySamples);
    preDelayR.setDelay(preDelaySamples);

    // Update plate feedback based on decay time
    float feedback = 0.5f + (decay / 10.0f) * 0.45f;

    for (auto& plate : plateNetworkL)
        for (auto& stage : plate)
        {
            stage.setFeedback(feedback);
            stage.setDecay(0.9f + decay * 0.009f);
        }

    for (auto& plate : plateNetworkR)
        for (auto& stage : plate)
        {
            stage.setFeedback(feedback);
            stage.setDecay(0.9f + decay * 0.009f);
        }

    // Update damping
    float dampFreq = 20000.0f * (1.0f - damping);
    for (auto& filter : dampingFiltersL)
        filter.setCutoff(dampFreq, sampleRate);
    for (auto& filter : dampingFiltersR)
        filter.setCutoff(dampFreq, sampleRate);

    // Update input/output filters
    auto highpassCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, lowCutFreq);
    auto lowpassCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, highCutFreq);

    inputHighpassL.coefficients = highpassCoeffs;
    inputHighpassR.coefficients = highpassCoeffs;
    outputLowpassL.coefficients = lowpassCoeffs;
    outputLowpassR.coefficients = lowpassCoeffs;

    // Update diffusion
    for (auto& diff : inputDiffusionL)
        diff.setFeedback(0.5f + diffusion * 0.3f);
    for (auto& diff : inputDiffusionR)
        diff.setFeedback(0.5f + diffusion * 0.3f);
}

void PlateReverbProcessor::updateModulation()
{
    modPhase += 2.0f * juce::MathConstants<float>::pi * modRate / static_cast<float>(sampleRate);
    if (modPhase > 2.0f * juce::MathConstants<float>::pi)
        modPhase -= 2.0f * juce::MathConstants<float>::pi;
}

// LatticeAllpass implementation
void PlateReverbProcessor::LatticeAllpass::setDelay(int samples)
{
    bufferSize = samples + 1;
    if (bufferSize <= 0) {
        bufferSize = 2; // Minimum size for allpass
    }

    buffer.resize(bufferSize, 0.0f);
    writeIndex = 0;
    readIndex = (bufferSize > 1) ? 1 : 0;
    z1 = 0.0f;
}

float PlateReverbProcessor::LatticeAllpass::process(float input)
{
    if (buffer.empty() || bufferSize <= 0)
        return input;

    // Ensure indices are within bounds
    if (writeIndex >= bufferSize) writeIndex = 0;
    if (readIndex >= bufferSize) readIndex = 0;

    buffer[writeIndex] = input;

    // Linear interpolation for smooth modulation
    float delayed = buffer[readIndex];
    int nextIndex = (readIndex + 1) % bufferSize;
    float nextDelayed = buffer[nextIndex];
    float fraction = 0.0f; // Could be modulated
    float interpolated = delayed + fraction * (nextDelayed - delayed);

    float feedforward = interpolated * feedback;
    float output = -input * feedback + interpolated + feedforward * decayFactor;

    buffer[writeIndex] = input + feedforward;

    writeIndex = (writeIndex + 1) % bufferSize;
    readIndex = (readIndex + 1) % bufferSize;

    return output;
}

void PlateReverbProcessor::LatticeAllpass::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    readIndex = 1;
    z1 = 0.0f;
}

void PlateReverbProcessor::LatticeAllpass::modulate(float amount)
{
    if (bufferSize <= 0) return;

    // Simple modulation by varying read position
    int modSamples = static_cast<int>(amount * bufferSize);
    readIndex = (writeIndex - bufferSize / 2 + modSamples + bufferSize) % bufferSize;

    // Ensure readIndex is within bounds
    if (readIndex >= bufferSize) readIndex = 0;
}

// OnePole implementation
void PlateReverbProcessor::OnePole::setCutoff(float freq, float sr)
{
    float omega = 2.0f * juce::MathConstants<float>::pi * freq / sr;
    float cosOmega = std::cos(omega);
    float alpha = (1.0f - cosOmega) / 2.0f;

    a0 = alpha;
    b1 = 1.0f - alpha;
}

float PlateReverbProcessor::OnePole::process(float input)
{
    float output = input * a0 + state * b1;
    state = output;
    return output;
}

// DelayLine implementation
void PlateReverbProcessor::DelayLine::setDelay(int samples)
{
    bufferSize = samples + 1;
    if (bufferSize <= 0) {
        bufferSize = 1;
    }

    buffer.resize(bufferSize, 0.0f);
    writeIndex = 0;
}

float PlateReverbProcessor::DelayLine::process(float input)
{
    if (buffer.empty() || bufferSize <= 0)
        return input;

    // Ensure writeIndex is within bounds
    if (writeIndex >= bufferSize) writeIndex = 0;

    int readIdx = (writeIndex - bufferSize + 1 + bufferSize) % bufferSize;
    float output = buffer[readIdx];
    buffer[writeIndex] = input;
    writeIndex = (writeIndex + 1) % bufferSize;
    return output;
}

void PlateReverbProcessor::DelayLine::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}
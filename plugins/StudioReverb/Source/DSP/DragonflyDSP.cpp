#include "DragonflyDSP.h"

DragonflyDSP::DragonflyDSP()
{
    early = std::make_unique<fv3::earlyref_f>();
    hallLate = std::make_unique<fv3::zrev2_f>();
    roomLate = std::make_unique<fv3::progenitor2_f>();
    plateLate = std::make_unique<fv3::nrev_f>();

    inputHPF_L = std::make_unique<fv3::iir_1st_f>();
    inputHPF_R = std::make_unique<fv3::iir_1st_f>();
    inputLPF_L = std::make_unique<fv3::iir_1st_f>();
    inputLPF_R = std::make_unique<fv3::iir_1st_f>();

    earlyOutL.resize(BUFFER_SIZE);
    earlyOutR.resize(BUFFER_SIZE);
    lateInL.resize(BUFFER_SIZE);
    lateInR.resize(BUFFER_SIZE);
    lateOutL.resize(BUFFER_SIZE);
    lateOutR.resize(BUFFER_SIZE);
}

DragonflyDSP::~DragonflyDSP() = default;

void DragonflyDSP::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Initialize early reflections
    early->setSampleRate(sampleRate);
    early->mute();

    // Initialize Hall reverb
    hallLate->setSampleRate(sampleRate);
    hallLate->mute();

    // Initialize Room reverb
    roomLate->setSampleRate(sampleRate);
    roomLate->mute();

    // Initialize Plate reverb
    plateLate->setSampleRate(sampleRate);
    plateLate->mute();

    // Initialize filters
    inputHPF_L->setSampleRate(sampleRate);
    inputHPF_R->setSampleRate(sampleRate);
    inputLPF_L->setSampleRate(sampleRate);
    inputLPF_R->setSampleRate(sampleRate);

    inputHPF_L->setHPF_1st(lowCut, 1.0f);
    inputHPF_R->setHPF_1st(lowCut, 1.0f);
    inputLPF_L->setLPF_1st(highCut, 1.0f);
    inputLPF_R->setLPF_1st(highCut, 1.0f);

    updateParameters();
}

void DragonflyDSP::reset()
{
    early->mute();
    hallLate->mute();
    roomLate->mute();
    plateLate->mute();
}

void DragonflyDSP::setReverbType(ReverbType type)
{
    if (currentType != type)
    {
        currentType = type;
        reset();
        updateParameters();
    }
}

void DragonflyDSP::setDryLevel(float value)
{
    dryLevel = juce::jlimit(0.0f, 1.0f, value);
}

void DragonflyDSP::setWetLevel(float value)
{
    wetLevel = juce::jlimit(0.0f, 1.0f, value);
}

void DragonflyDSP::setSize(float value)
{
    roomSize = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}

void DragonflyDSP::setPreDelay(float value)
{
    preDelay = juce::jlimit(0.0f, 100.0f, value); // ms
    updateParameters();
}

void DragonflyDSP::setDamping(float value)
{
    damping = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}

void DragonflyDSP::setLowCut(float value)
{
    lowCut = juce::jlimit(20.0f, 500.0f, value);
    if (inputHPF_L && inputHPF_R)
    {
        inputHPF_L->setHPF_1st(lowCut, 1.0f);
        inputHPF_R->setHPF_1st(lowCut, 1.0f);
    }
}

void DragonflyDSP::setHighCut(float value)
{
    highCut = juce::jlimit(1000.0f, 20000.0f, value);
    if (inputLPF_L && inputLPF_R)
    {
        inputLPF_L->setLPF_1st(highCut, 1.0f);
        inputLPF_R->setLPF_1st(highCut, 1.0f);
    }
}

void DragonflyDSP::setDecay(float value)
{
    decay = juce::jlimit(0.1f, 10.0f, value);
    updateParameters();
}

void DragonflyDSP::updateParameters()
{
    // Update early reflections (common to all types)
    early->setRSFactor(roomSize * 30.0f + 10.0f); // Room size 10-40m
    early->setPreDelay(preDelay * currentSampleRate / 1000.0f);

    switch (currentType)
    {
        case ReverbType::Hall:
        {
            hallLate->setrt60(decay);
            hallLate->setidiffusion1(0.75f + roomSize * 0.2f);
            hallLate->setidiffusion2(0.625f + roomSize * 0.15f);
            hallLate->setdiffusion1(0.7f);
            hallLate->setdiffusion2(0.5f);
            hallLate->setdamp(damping);
            hallLate->setinputdamp(damping * 0.5f);
            hallLate->setdamp2(damping * 0.7f);
            hallLate->setPreDelay(preDelay * currentSampleRate / 1000.0f);
            break;
        }

        case ReverbType::Room:
        {
            roomLate->setrt60(decay);
            roomLate->setidiffusion1(0.65f + roomSize * 0.25f);
            roomLate->setidiffusion2(0.5f + roomSize * 0.2f);
            roomLate->setdiffusion1(0.65f);
            roomLate->setdiffusion2(0.45f);
            roomLate->setdamp(damping * 1.2f);
            roomLate->setinputdamp(damping * 0.6f);
            roomLate->setdamp2(damping * 0.8f);
            roomLate->setPreDelay(preDelay * currentSampleRate / 1000.0f);
            break;
        }

        case ReverbType::Plate:
        {
            plateLate->setrt60(decay);
            plateLate->setidiffusion1(0.75f);
            plateLate->setidiffusion2(0.625f);
            plateLate->setodiffusion1(0.7f);
            plateLate->setodiffusion2(0.5f);

            float bandwidth = 1.0f - damping;
            plateLate->setdamp(bandwidth);
            plateLate->setdamp2(bandwidth * 0.8f);
            plateLate->setPreDelay(preDelay * currentSampleRate / 1000.0f);
            break;
        }

        case ReverbType::Early:
            // Early reflections only - parameters already set above
            break;
    }
}

void DragonflyDSP::processBlock(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels < 2)
        return;

    float* inputL = buffer.getWritePointer(0);
    float* inputR = buffer.getWritePointer(1);
    float* outputL = buffer.getWritePointer(0);
    float* outputR = buffer.getWritePointer(1);

    switch (currentType)
    {
        case ReverbType::Hall:
            processHall(inputL, inputR, outputL, outputR, numSamples);
            break;

        case ReverbType::Room:
            processRoom(inputL, inputR, outputL, outputR, numSamples);
            break;

        case ReverbType::Plate:
            processPlate(inputL, inputR, outputL, outputR, numSamples);
            break;

        case ReverbType::Early:
            processEarly(inputL, inputR, outputL, outputR, numSamples);
            break;
    }
}

void DragonflyDSP::processHall(float* inputL, float* inputR, float* outputL, float* outputR, int numSamples)
{
    // Process in blocks for efficiency
    for (int offset = 0; offset < numSamples; offset += BUFFER_SIZE)
    {
        int blockSize = std::min(BUFFER_SIZE, numSamples - offset);

        // Apply input filters
        for (int i = 0; i < blockSize; ++i)
        {
            float filteredL = inputHPF_L->process(inputL[offset + i]);
            float filteredR = inputHPF_R->process(inputR[offset + i]);

            lateInL[i] = inputLPF_L->process(filteredL) * 0.5f;
            lateInR[i] = inputLPF_R->process(filteredR) * 0.5f;
        }

        // Process early reflections
        early->processreplace(lateInL.data(), lateInR.data(),
                            earlyOutL.data(), earlyOutR.data(), blockSize);

        // Mix early output with late input (early send)
        for (int i = 0; i < blockSize; ++i)
        {
            lateInL[i] = lateInL[i] * 0.8f + earlyOutL[i] * 0.2f;
            lateInR[i] = lateInR[i] * 0.8f + earlyOutR[i] * 0.2f;
        }

        // Process late reverb
        hallLate->processreplace(lateInL.data(), lateInR.data(),
                                lateOutL.data(), lateOutR.data(), blockSize);

        // Mix outputs
        for (int i = 0; i < blockSize; ++i)
        {
            float wetL = (earlyOutL[i] * 0.2f + lateOutL[i] * 0.8f) * wetLevel;
            float wetR = (earlyOutR[i] * 0.2f + lateOutR[i] * 0.8f) * wetLevel;

            outputL[offset + i] = inputL[offset + i] * dryLevel + wetL;
            outputR[offset + i] = inputR[offset + i] * dryLevel + wetR;
        }
    }
}

void DragonflyDSP::processRoom(float* inputL, float* inputR, float* outputL, float* outputR, int numSamples)
{
    for (int offset = 0; offset < numSamples; offset += BUFFER_SIZE)
    {
        int blockSize = std::min(BUFFER_SIZE, numSamples - offset);

        // Apply input filters
        for (int i = 0; i < blockSize; ++i)
        {
            float filteredL = inputHPF_L->process(inputL[offset + i]);
            float filteredR = inputHPF_R->process(inputR[offset + i]);

            lateInL[i] = inputLPF_L->process(filteredL) * 0.5f;
            lateInR[i] = inputLPF_R->process(filteredR) * 0.5f;
        }

        // Process early reflections
        early->processreplace(lateInL.data(), lateInR.data(),
                            earlyOutL.data(), earlyOutR.data(), blockSize);

        // Mix early output with late input
        for (int i = 0; i < blockSize; ++i)
        {
            lateInL[i] = lateInL[i] * 0.7f + earlyOutL[i] * 0.3f;
            lateInR[i] = lateInR[i] * 0.7f + earlyOutR[i] * 0.3f;
        }

        // Process late reverb
        roomLate->processreplace(lateInL.data(), lateInR.data(),
                                lateOutL.data(), lateOutR.data(), blockSize);

        // Mix outputs
        for (int i = 0; i < blockSize; ++i)
        {
            float wetL = (earlyOutL[i] * 0.3f + lateOutL[i] * 0.7f) * wetLevel;
            float wetR = (earlyOutR[i] * 0.3f + lateOutR[i] * 0.7f) * wetLevel;

            outputL[offset + i] = inputL[offset + i] * dryLevel + wetL;
            outputR[offset + i] = inputR[offset + i] * dryLevel + wetR;
        }
    }
}

void DragonflyDSP::processPlate(float* inputL, float* inputR, float* outputL, float* outputR, int numSamples)
{
    for (int offset = 0; offset < numSamples; offset += BUFFER_SIZE)
    {
        int blockSize = std::min(BUFFER_SIZE, numSamples - offset);

        // Apply input filters
        for (int i = 0; i < blockSize; ++i)
        {
            float filteredL = inputHPF_L->process(inputL[offset + i]);
            float filteredR = inputHPF_R->process(inputR[offset + i]);

            lateInL[i] = inputLPF_L->process(filteredL) * 0.5f;
            lateInR[i] = inputLPF_R->process(filteredR) * 0.5f;
        }

        // Process plate reverb (no early reflections for plate)
        plateLate->processreplace(lateInL.data(), lateInR.data(),
                                lateOutL.data(), lateOutR.data(), blockSize);

        // Mix outputs
        for (int i = 0; i < blockSize; ++i)
        {
            float wetL = lateOutL[i] * wetLevel;
            float wetR = lateOutR[i] * wetLevel;

            outputL[offset + i] = inputL[offset + i] * dryLevel + wetL;
            outputR[offset + i] = inputR[offset + i] * dryLevel + wetR;
        }
    }
}

void DragonflyDSP::processEarly(float* inputL, float* inputR, float* outputL, float* outputR, int numSamples)
{
    for (int offset = 0; offset < numSamples; offset += BUFFER_SIZE)
    {
        int blockSize = std::min(BUFFER_SIZE, numSamples - offset);

        // Apply input filters
        for (int i = 0; i < blockSize; ++i)
        {
            lateInL[i] = inputL[offset + i] * 0.5f;
            lateInR[i] = inputR[offset + i] * 0.5f;
        }

        // Process early reflections only
        early->processreplace(lateInL.data(), lateInR.data(),
                            earlyOutL.data(), earlyOutR.data(), blockSize);

        // Mix outputs
        for (int i = 0; i < blockSize; ++i)
        {
            outputL[offset + i] = inputL[offset + i] * dryLevel + earlyOutL[i] * wetLevel;
            outputR[offset + i] = inputR[offset + i] * dryLevel + earlyOutR[i] * wetLevel;
        }
    }
}
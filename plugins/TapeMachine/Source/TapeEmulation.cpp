#include "TapeEmulation.h"

TapeEmulation::TapeEmulation()
{
    reset();
}

void TapeEmulation::prepare(double sampleRate)
{
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    currentSampleRate = sampleRate;
    reset();

    preEmphasisFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 3000.0f, 0.7f, 1.5f);

    deEmphasisFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sampleRate, 100.0f, 0.7f, 1.3f);

    headBumpFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, 60.0f, 0.5f, 1.8f);

    tapeResponseFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, 15000.0f, 0.7f);
}

void TapeEmulation::reset()
{
    preEmphasisFilter.reset();
    deEmphasisFilter.reset();
    headBumpFilter.reset();
    tapeResponseFilter.reset();

    previousInput = 0.0f;
    previousOutput = 0.0f;
    hysteresisState = 0.0f;

    delayLine.fill(0.0f);
    delayIndex = 0;
}

TapeEmulation::MachineCharacteristics TapeEmulation::getMachineCharacteristics(TapeMachine machine)
{
    MachineCharacteristics chars;

    switch (machine)
    {
        case StuderA800:
            chars.lowFreqBoost = 1.2f;
            chars.highFreqRoll = 0.85f;
            chars.saturationCurve = 0.7f;
            chars.compressionRatio = 0.15f;
            chars.harmonicProfile = 0.6f;
            break;

        case AmpexATR102:
            chars.lowFreqBoost = 1.05f;
            chars.highFreqRoll = 0.95f;
            chars.saturationCurve = 0.8f;
            chars.compressionRatio = 0.1f;
            chars.harmonicProfile = 0.4f;
            break;

        case Blend:
        default:
            chars.lowFreqBoost = 1.125f;
            chars.highFreqRoll = 0.9f;
            chars.saturationCurve = 0.75f;
            chars.compressionRatio = 0.125f;
            chars.harmonicProfile = 0.5f;
            break;
    }

    return chars;
}

TapeEmulation::TapeCharacteristics TapeEmulation::getTapeCharacteristics(TapeType type)
{
    TapeCharacteristics chars;

    switch (type)
    {
        case Ampex456:
            chars.hysteresis = 0.3f;
            chars.coercivity = 0.8f;
            chars.retentivity = 0.9f;
            chars.saturationLevel = 0.85f;
            chars.noiseFloor = 0.02f;
            break;

        case GP9:
            chars.hysteresis = 0.25f;
            chars.coercivity = 0.75f;
            chars.retentivity = 0.85f;
            chars.saturationLevel = 0.9f;
            chars.noiseFloor = 0.015f;
            break;

        case BASF911:
            chars.hysteresis = 0.35f;
            chars.coercivity = 0.85f;
            chars.retentivity = 0.88f;
            chars.saturationLevel = 0.82f;
            chars.noiseFloor = 0.018f;
            break;

        default:
            chars.hysteresis = 0.3f;
            chars.coercivity = 0.8f;
            chars.retentivity = 0.87f;
            chars.saturationLevel = 0.85f;
            chars.noiseFloor = 0.02f;
            break;
    }

    return chars;
}

void TapeEmulation::updateFilters(TapeSpeed speed, TapeMachine machine)
{
    if (currentSampleRate <= 0.0)
        return;

    float speedMultiplier = 1.0f;
    float headBumpFreq = 60.0f;
    float highCutoff = 15000.0f;

    switch (speed)
    {
        case Speed_7_5_IPS:
            speedMultiplier = 0.5f;
            headBumpFreq = 50.0f;
            highCutoff = 10000.0f;
            break;

        case Speed_15_IPS:
            speedMultiplier = 1.0f;
            headBumpFreq = 60.0f;
            highCutoff = 15000.0f;
            break;

        case Speed_30_IPS:
            speedMultiplier = 2.0f;
            headBumpFreq = 80.0f;
            highCutoff = 18000.0f;
            break;
    }

    auto machineChars = getMachineCharacteristics(machine);

    headBumpFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate, headBumpFreq, 0.5f, 1.0f + (0.8f * machineChars.lowFreqBoost));

    tapeResponseFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        currentSampleRate, highCutoff * machineChars.highFreqRoll, 0.7f);
}

float TapeEmulation::applyHysteresis(float input, float hysteresisAmount)
{
    float drive = 0.5f + hysteresisAmount;
    float mix = hysteresisAmount * 0.5f;

    float driven = input * drive;
    float diff = driven - hysteresisState;

    hysteresisState += diff * (1.0f - hysteresisAmount * 0.3f);

    float output = hysteresisState + (diff * mix);

    return juce::jlimit(-1.0f, 1.0f, output);
}

float TapeEmulation::applyCrossoverDistortion(float input, float amount)
{
    if (std::abs(input) < denormalPrevention)
        return 0.0f;

    float threshold = 0.05f * (1.0f - amount * 0.5f);
    float absInput = std::abs(input);

    if (absInput < threshold)
    {
        float scale = 1.0f - amount * 0.3f;
        return input * scale;
    }

    return input;
}

float TapeEmulation::applyMagneticSaturation(float input, float saturationLevel, float coercivity)
{
    if (std::abs(input) < denormalPrevention)
        return 0.0f;

    float drive = 1.0f + (coercivity * 2.0f);
    float x = input * drive;

    float tanh_component = std::tanh(x * saturationLevel);

    float langevin = x / (1.0f + std::abs(x));

    float output = tanh_component * 0.7f + langevin * 0.3f;

    return output * 0.95f;
}

float TapeEmulation::processSample(float input, TapeMachine machine, TapeSpeed speed, TapeType type)
{
    if (std::abs(input) < denormalPrevention)
        return 0.0f;

    updateFilters(speed, machine);

    auto machineChars = getMachineCharacteristics(machine);
    auto tapeChars = getTapeCharacteristics(type);

    float processed = preEmphasisFilter.processSample(input);

    processed = applyHysteresis(processed, tapeChars.hysteresis);

    processed = applyMagneticSaturation(processed, tapeChars.saturationLevel, tapeChars.coercivity);

    processed = applyCrossoverDistortion(processed, machineChars.harmonicProfile);

    processed = headBumpFilter.processSample(processed);
    processed = tapeResponseFilter.processSample(processed);
    processed = deEmphasisFilter.processSample(processed);

    delayLine[static_cast<size_t>(delayIndex)] = processed;
    delayIndex = (delayIndex + 1) & 3;

    float delayed = delayLine[static_cast<size_t>(delayIndex)] * 0.1f;
    processed += delayed * tapeChars.retentivity * 0.05f;

    float compressionAmount = machineChars.compressionRatio;
    float threshold = 0.7f;
    if (std::abs(processed) > threshold)
    {
        float over = std::abs(processed) - threshold;
        float reduction = 1.0f - (over * compressionAmount);
        processed *= reduction;
    }

    return juce::jlimit(-1.0f, 1.0f, processed);
}
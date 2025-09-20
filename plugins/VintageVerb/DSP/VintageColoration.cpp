/*
  ==============================================================================

    VintageColoration.cpp - Implementation of era-specific processing

  ==============================================================================
*/

#include "VintageColoration.h"
#include <cmath>

VintageColoration::VintageColoration()
{
}

void VintageColoration::prepare(double sr, int maxBlockSize)
{
    sampleRate = sr;

    // Prepare filters
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
    spec.numChannels = 1;

    filterL.prepare(spec);
    filterR.prepare(spec);

    modern.prepare(sr);

    // Configure for default mode
    setColorMode(currentMode);

    reset();
}

void VintageColoration::reset()
{
    filterL.lowpass.reset();
    filterL.highpass.reset();
    filterL.tiltEQ.reset();
    filterR.lowpass.reset();
    filterR.highpass.reset();
    filterR.tiltEQ.reset();

    vintage70s.transformerL.reset();
    vintage70s.transformerR.reset();
    vintage70s.humPhase = 0.0f;

    vintage80s.decimatorL.lastSample = 0.0f;
    vintage80s.decimatorL.counter = 0;
    vintage80s.decimatorR.lastSample = 0.0f;
    vintage80s.decimatorR.counter = 0;
}

void VintageColoration::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int numChannels = buffer.getNumChannels();
    if (numChannels < 2) return;

    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getWritePointer(1);

    processStereo(leftChannel, rightChannel, leftChannel, rightChannel, numSamples);
}

void VintageColoration::processStereo(float* leftIn, float* rightIn,
                                     float* leftOut, float* rightOut, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float left = leftIn[i];
        float right = rightIn[i];

        // Apply era-specific processing
        switch (currentMode)
        {
            case Color1970s:
                vintage70s.process(left, right, intensity);
                break;

            case Color1980s:
                vintage80s.process(left, right, intensity);
                break;

            case ColorNow:
                modern.process(left, right, intensity * 0.3f);  // Subtle enhancement
                break;
        }

        // Apply era-specific filtering
        left = filterL.process(left);
        right = filterR.process(right);

        // Soft clipping for safety
        leftOut[i] = softClip(left);
        rightOut[i] = softClip(right);
    }
}

void VintageColoration::setColorMode(ColorMode mode)
{
    currentMode = mode;

    switch (mode)
    {
        case Color1970s:
            filterL.configure1970s(sampleRate);
            filterR.configure1970s(sampleRate);
            vintage80s.decimatorL.setSampleRate(sampleRate, 22050.0);  // Lo-fi
            vintage80s.decimatorR.setSampleRate(sampleRate, 22050.0);
            break;

        case Color1980s:
            filterL.configure1980s(sampleRate);
            filterR.configure1980s(sampleRate);
            vintage80s.decimatorL.setSampleRate(sampleRate, 32000.0);  // Early digital
            vintage80s.decimatorR.setSampleRate(sampleRate, 32000.0);
            break;

        case ColorNow:
            filterL.configureModern(sampleRate);
            filterR.configureModern(sampleRate);
            break;
    }
}

void VintageColoration::setIntensity(float amount)
{
    intensity = juce::jlimit(0.0f, 1.0f, amount);
}

void VintageColoration::setNoiseAmount(float amount)
{
    noiseAmount = juce::jlimit(0.0f, 1.0f, amount);
}

void VintageColoration::setArtifactAmount(float amount)
{
    artifactAmount = juce::jlimit(0.0f, 1.0f, amount);
}

// Vintage1970s implementation
float VintageColoration::Vintage1970s::saturate(float input, float amount)
{
    const float drive = 1.0f + amount * 4.0f;
    float x = input * drive;

    // Asymmetric tube-like saturation
    if (x > 0.0f)
        return std::tanh(x * 0.7f) / drive;
    else
        return std::tanh(x * 0.9f) / drive;
}

float VintageColoration::Vintage1970s::TransformerModel::process(float input)
{
    // Simple transformer hysteresis model
    const float saturation = 0.8f;
    const float hysteresisAmount = 0.1f;

    float output = std::tanh(input * saturation);
    hysteresis = hysteresis * 0.95f + (output - lastOut) * hysteresisAmount;
    output += hysteresis;
    lastOut = output;

    return output;
}

void VintageColoration::Vintage1970s::TransformerModel::reset()
{
    lastOut = 0.0f;
    hysteresis = 0.0f;
}

float VintageColoration::Vintage1970s::NoiseGenerator::generatePink()
{
    // Paul Kellet's pink noise filter
    float white = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;

    pinkFilters[0] = 0.99886f * pinkFilters[0] + white * 0.0555179f;
    pinkFilters[1] = 0.99332f * pinkFilters[1] + white * 0.0750759f;
    pinkFilters[2] = 0.96900f * pinkFilters[2] + white * 0.1538520f;
    pinkFilters[3] = 0.86650f * pinkFilters[3] + white * 0.3104856f;
    pinkFilters[4] = 0.55000f * pinkFilters[4] + white * 0.5329522f;
    pinkFilters[5] = -0.7616f * pinkFilters[5] + white * 0.0168980f;

    pink = pinkFilters[0] + pinkFilters[1] + pinkFilters[2] + pinkFilters[3]
         + pinkFilters[4] + pinkFilters[5] + pinkFilters[6] + white * 0.5362f;
    pinkFilters[6] = white * 0.115926f;

    return pink * 0.11f;  // Scale to reasonable level
}

float VintageColoration::Vintage1970s::NoiseGenerator::generateBrown()
{
    float white = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
    brown = (brown + (0.02f * white)) / 1.02f;
    return brown * 3.5f;
}

float VintageColoration::Vintage1970s::NoiseGenerator::generate60HzHum(float phase)
{
    // 60Hz hum with 2nd harmonic
    float hum = std::sin(phase) * 0.7f;
    hum += std::sin(phase * 2.0f) * 0.3f;  // 120Hz harmonic
    return hum;
}

void VintageColoration::Vintage1970s::process(float& left, float& right, float intensity)
{
    // Add vintage noise floor
    float noiseL = noise.generatePink() * 0.001f * intensity;
    float noiseR = noise.generatePink() * 0.001f * intensity;

    // Add subtle 60Hz hum
    humPhase += (60.0f * 2.0f * juce::MathConstants<float>::pi) / 44100.0f;
    if (humPhase > juce::MathConstants<float>::twoPi)
        humPhase -= juce::MathConstants<float>::twoPi;

    float hum = noise.generate60HzHum(humPhase) * 0.0001f * intensity;

    left += noiseL + hum;
    right += noiseR + hum * 0.9f;  // Slightly different hum level for stereo

    // Transformer saturation
    left = transformerL.process(left) * (1.0f - intensity * 0.3f) + left * intensity * 0.3f;
    right = transformerR.process(right) * (1.0f - intensity * 0.3f) + right * intensity * 0.3f;

    // Tube-like saturation
    left = saturate(left, intensity * 0.5f);
    right = saturate(right, intensity * 0.5f);
}

// Vintage1980s implementation
float VintageColoration::Vintage1980s::bitCrush(float input, int bits)
{
    if (bits >= 24) return input;

    float maxVal = std::pow(2.0f, bits - 1) - 1.0f;
    float quantized = std::round(input * maxVal) / maxVal;
    return quantized;
}

float VintageColoration::Vintage1980s::SampleRateReducer::process(float input)
{
    if (++counter >= holdTime)
    {
        counter = 0;
        lastSample = input;
    }
    return lastSample;
}

void VintageColoration::Vintage1980s::SampleRateReducer::setSampleRate(double hostRate, double targetRate)
{
    holdTime = std::max(1, static_cast<int>(hostRate / targetRate));
}

float VintageColoration::Vintage1980s::AliasingGenerator::process(float input)
{
    // Simple aliasing by naive downsampling/upsampling
    float diff = input - lastIn;
    float alias = diff * diff * ((diff > 0) ? 1.0f : -1.0f);
    lastIn = input;
    lastOut = lastOut * 0.8f + alias * 0.2f;
    return input + lastOut * 0.05f;  // Subtle aliasing
}

float VintageColoration::Vintage1980s::CompandingArtifacts::muLawEncode(float input)
{
    const float mu = 255.0f;
    float sign = (input < 0) ? -1.0f : 1.0f;
    float absInput = std::abs(input);
    return sign * std::log(1.0f + mu * absInput) / std::log(1.0f + mu);
}

float VintageColoration::Vintage1980s::CompandingArtifacts::muLawDecode(float input)
{
    const float mu = 255.0f;
    float sign = (input < 0) ? -1.0f : 1.0f;
    float absInput = std::abs(input);
    return sign * ((std::pow(1.0f + mu, absInput) - 1.0f) / mu);
}

float VintageColoration::Vintage1980s::CompandingArtifacts::process(float input, float amount)
{
    float encoded = muLawEncode(input);
    float decoded = muLawDecode(encoded);
    return input * (1.0f - amount) + decoded * amount;
}

void VintageColoration::Vintage1980s::process(float& left, float& right, float intensity)
{
    // Sample rate reduction
    left = decimatorL.process(left);
    right = decimatorR.process(right);

    // Bit crushing (12-bit for strong effect, 16-bit for subtle)
    int bitDepth = static_cast<int>(16 - intensity * 4);
    left = bitCrush(left, bitDepth);
    right = bitCrush(right, bitDepth);

    // Aliasing artifacts
    left = aliasingL.process(left);
    right = aliasingR.process(right);

    // Companding artifacts (early digital compression)
    left = compander.process(left, intensity * 0.3f);
    right = compander.process(right, intensity * 0.3f);
}

// ModernProcessing implementation
float VintageColoration::ModernProcessing::HarmonicExciter::process(float input)
{
    // Extract high frequencies
    float highFreq = highpass.processSample(0, input);

    // Generate harmonics through soft clipping
    float excited = std::tanh(highFreq * 3.0f);

    // Mix back subtle harmonics
    return input + excited * 0.05f;
}

void VintageColoration::ModernProcessing::HarmonicExciter::setFrequency(float freq)
{
    frequency = freq;
    highpass.setCutoffFrequency(frequency);
}

void VintageColoration::ModernProcessing::HarmonicExciter::prepare(const juce::dsp::ProcessSpec& spec)
{
    highpass.prepare(spec);
    highpass.setCutoffFrequency(frequency);
    highpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
}

void VintageColoration::ModernProcessing::StereoEnhancer::process(float& left, float& right, float width)
{
    float mid = (left + right) * 0.5f;
    float side = (left - right) * 0.5f;

    // Enhance side signal with micro delays
    delayL.pushSample(0, side);
    delayR.pushSample(0, side);

    float delayedSideL = delayL.popSample(0, 0.2f);  // 0.2ms
    float delayedSideR = delayR.popSample(0, 0.3f);  // 0.3ms

    side = side * (1.0f + width) + (delayedSideL - delayedSideR) * width * 0.1f;

    left = mid + side;
    right = mid - side;
}

void VintageColoration::ModernProcessing::StereoEnhancer::prepare(const juce::dsp::ProcessSpec& spec)
{
    delayL.prepare(spec);
    delayR.prepare(spec);
}

void VintageColoration::ModernProcessing::process(float& left, float& right, float intensity)
{
    // Harmonic excitement for presence
    left = exciterL.process(left);
    right = exciterR.process(right);

    // Stereo width enhancement
    widener.process(left, right, intensity);
}

void VintageColoration::ModernProcessing::prepare(double sampleRate)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 512;
    spec.numChannels = 1;

    exciterL.prepare(spec);
    exciterL.setFrequency(8000.0f);

    exciterR.prepare(spec);
    exciterR.setFrequency(8000.0f);

    widener.prepare(spec);
}

// EraFilter implementation
void VintageColoration::EraFilter::prepare(const juce::dsp::ProcessSpec& spec)
{
    lowpass.prepare(spec);
    highpass.prepare(spec);
    tiltEQ.prepare(spec);
}

void VintageColoration::EraFilter::configure1970s(double sampleRate)
{
    // Dark, rolled-off highs
    lowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    lowpass.setCutoffFrequency(8000.0f);
    lowpass.setResonance(0.7f);

    highpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highpass.setCutoffFrequency(100.0f);
    highpass.setResonance(0.7f);

    // Warm tilt
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sampleRate, 200.0f, 0.7f, 1.2f);
    tiltEQ.coefficients = coeffs;
}

void VintageColoration::EraFilter::configure1980s(double sampleRate)
{
    // Brighter, more presence
    lowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    lowpass.setCutoffFrequency(12000.0f);
    lowpass.setResonance(0.8f);

    highpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highpass.setCutoffFrequency(80.0f);
    highpass.setResonance(0.6f);

    // Bright tilt
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 5000.0f, 0.7f, 1.15f);
    tiltEQ.coefficients = coeffs;
}

void VintageColoration::EraFilter::configureModern(double sampleRate)
{
    // Full bandwidth, transparent
    lowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    lowpass.setCutoffFrequency(20000.0f);
    lowpass.setResonance(0.707f);

    highpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highpass.setCutoffFrequency(20.0f);
    highpass.setResonance(0.707f);

    // Flat response
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, 1000.0f, 1.0f, 1.0f);
    tiltEQ.coefficients = coeffs;
}

float VintageColoration::EraFilter::process(float input)
{
    float filtered = highpass.processSample(0, input);
    filtered = lowpass.processSample(0, filtered);
    filtered = tiltEQ.processSample(filtered);
    return filtered;
}

// Helper functions
float VintageColoration::softClip(float input)
{
    if (std::abs(input) < 0.5f)
        return input;
    else
        return std::tanh(input * 0.7f) * 1.43f;
}

float VintageColoration::crossfade(float a, float b, float mix)
{
    return a * (1.0f - mix) + b * mix;
}
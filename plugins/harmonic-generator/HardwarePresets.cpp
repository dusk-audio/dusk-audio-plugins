/*
  ==============================================================================

    HardwarePresets.cpp - Implementation of hardware saturation emulations

  ==============================================================================
*/

#include "HardwarePresets.h"
#include <cmath>

//==============================================================================
// HardwareSaturation Implementation
//==============================================================================

HardwareSaturation::HardwareSaturation()
{
}

void HardwareSaturation::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;

    // Prepare emulation components
    tapeEmulation.prepare(sampleRate);
    tubeEmulation.prepare(sampleRate);
    transistorEmulation.prepare(sampleRate);

    // Setup tone filter (lowpass filter for brightness control)
    // Prepare for stereo to handle both channels
    toneFilter.prepare({ sampleRate, 512, 2 });
    toneFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    toneFilter.setCutoffFrequency(10000.0f);  // Default to bright
}

void HardwareSaturation::reset()
{
    toneFilter.reset();
    // Reset all emulation components
    tapeEmulation.reset();
    tubeEmulation.reset();
    transistorEmulation.reset();
}

void HardwareSaturation::setMode(Mode mode)
{
    currentMode = mode;
}

const HardwareSaturation::HarmonicProfile& HardwareSaturation::getCachedProfile() const
{
    // Note: cachedMode and cachedProfile are declared mutable in header
    // This allows modification in const method for caching purposes
    if (cachedMode != currentMode)
    {
        cachedProfile = getProfileForMode(currentMode);
        cachedMode = currentMode;
    }
    return cachedProfile;
}

void HardwareSaturation::setDrive(float amount)
{
    drive = juce::jlimit(0.0f, 1.0f, amount);
}

void HardwareSaturation::setMix(float wetDry)
{
    mix = juce::jlimit(0.0f, 1.0f, wetDry);
}

void HardwareSaturation::setOutput(float gain)
{
    output = juce::Decibels::decibelsToGain(juce::jlimit(-12.0f, 12.0f, gain));
}

void HardwareSaturation::setTone(float brightness)
{
    tone = juce::jlimit(-1.0f, 1.0f, brightness);

    // Adjust tone filter cutoff based on brightness control
    // -1 = dark (5kHz), 0 = neutral (10kHz), +1 = bright (20kHz)
    float cutoff = 10000.0f * std::pow(2.0f, tone);  // ±1 octave
    toneFilter.setCutoffFrequency(cutoff);
}

float HardwareSaturation::processSample(float input, int channel)
{
    // Store dry signal for mix
    float dry = input;

    // Get current profile (consider caching this if mode doesn't change often)
    const HarmonicProfile& profile = getCachedProfile();

    // Apply drive
    float driven = input * (1.0f + drive * 4.0f);  // Up to 5x drive

    // Process through appropriate emulation based on mode category
    float processed = 0.0f;

    if (currentMode == Mode::StuderA800 ||
        currentMode == Mode::AmpexATR102 ||
        currentMode == Mode::TascamPorta)
    {
        // Tape emulation
        processed = tapeEmulation.process(driven, profile);
    }
    else if (currentMode == Mode::FairchildTube ||
             currentMode == Mode::PultecEQP1A ||
             currentMode == Mode::UA610 ||
             currentMode == Mode::CultureVulture ||
             currentMode == Mode::BlackBox)
    {
        // Tube emulation
        processed = tubeEmulation.process(driven, profile);
    }
    else
    {
        // Transistor emulation (Neve, API, SSL, Decapitator)
        processed = transistorEmulation.process(driven, profile);
    }

    // Apply general saturation curve with harmonic profile
    processed = applySaturation(processed, profile);

    // Apply tone control (validate channel index for stereo)
    int validChannel = juce::jlimit(0, 1, channel);
    processed = toneFilter.processSample(validChannel, processed);

    // Apply output gain
    processed *= output;

    // Mix dry/wet
    float wet = processed * mix;
    float dryMix = dry * (1.0f - mix);

    return wet + dryMix;
}

//==============================================================================
// Saturation Algorithms
//==============================================================================

float HardwareSaturation::applySaturation(float input, const HarmonicProfile& profile)
{
    // Generate harmonics based on profile
    float x = input;
    float x2 = x * x;
    float x3 = x2 * x;
    float x4 = x2 * x2;
    float x5 = x4 * x;

    // Apply harmonic content
    float result = x;

    // 2nd harmonic (even - warmth)
    if (profile.h2 > 0.0f)
        result += profile.h2 * x2;

    // 3rd harmonic (odd - aggression)
    if (profile.h3 > 0.0f)
        result += profile.h3 * x3;

    // 4th harmonic (even)
    if (profile.h4 > 0.0f)
        result += profile.h4 * x4;

    // 5th harmonic (odd)
    if (profile.h5 > 0.0f)
        result += profile.h5 * x5;

    // Apply saturation curve (blend between soft and hard)
    float soft = tanhSaturation(result, 0.7f);
    float hard = hardClip(result, 0.8f);
    result = soft * (1.0f - profile.saturationCurve) + hard * profile.saturationCurve;

    // Apply asymmetric clipping if specified
    if (profile.asymmetric)
        result = asymmetricSaturation(result, 0.3f);

    // Soft compression
    if (profile.compressionAmount > 0.0f)
    {
        float threshold = 0.5f;
        float absResult = std::abs(result);
        if (absResult > threshold)
        {
            float excess = absResult - threshold;
            float compressed = threshold + excess * (1.0f - profile.compressionAmount);
            result = (result >= 0 ? 1.0f : -1.0f) * compressed;
        }
    }

    return result;
}

float HardwareSaturation::softClip(float input, float threshold)
{
    if (std::abs(input) < threshold)
        return input;

    float sign = input >= 0 ? 1.0f : -1.0f;
    float excess = std::abs(input) - threshold;
    return sign * (threshold + std::tanh(excess));
}

float HardwareSaturation::hardClip(float input, float threshold)
{
    return juce::jlimit(-threshold, threshold, input);
}

float HardwareSaturation::tanhSaturation(float input, float amount)
{
    // Protect against division by zero
    if (std::abs(amount) < 1e-6f)
        return input;  // Linear passthrough for very small amounts

    return std::tanh(input * amount) / amount;
}

float HardwareSaturation::asymmetricSaturation(float input, float amount)
{
    // Positive and negative sides clip differently (like real hardware)
    if (input >= 0)
        return std::tanh(input * (1.0f + amount));
    else
        return std::tanh(input * (1.0f - amount * 0.5f));
}

//==============================================================================
// TapeEmulation Implementation
//==============================================================================

void HardwareSaturation::TapeEmulation::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;

    // Pre-emphasis filter (boost highs before saturation)
    auto preCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 3000.0f, 0.707f, 2.0f);
    *preEmphasis.coefficients = *preCoeffs;

    // De-emphasis filter (reduce highs after saturation)
    auto deCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 3000.0f, 0.707f, 0.5f);
    *deEmphasis.coefficients = *deCoeffs;

    preEmphasis.reset();
    deEmphasis.reset();
}

void HardwareSaturation::TapeEmulation::reset()
{
    hysteresisState = 0.0f;
    previousInput = 0.0f;
    lpState = 0.0f;
    preEmphasis.reset();
    deEmphasis.reset();
}

float HardwareSaturation::TapeEmulation::process(float input, const HarmonicProfile& profile)
{
    // Apply pre-emphasis
    float emphasized = preEmphasis.processSample(input);

    // Tape hysteresis (simple model)
    float delta = emphasized - previousInput;
    // Apply DC-blocking multiplier to prevent unbounded accumulation
    hysteresisState *= 0.9995f;  // Gentle decay to prevent DC drift
    hysteresisState += delta * 0.7f + hysteresisState * 0.3f * std::tanh(delta * 2.0f);

    // Denormal protection
    if (std::abs(hysteresisState) < 1e-10f)
        hysteresisState = 0.0f;

    previousInput = emphasized;

    // Tape saturation curve (soft, warm)
    float saturated = std::tanh(hysteresisState * 1.5f) * 0.85f;

    // Apply bias (reduces distortion at low levels)
    float biased = saturated + biasAmount * 0.01f * std::sin(hysteresisState * 10.0f);

    // Apply de-emphasis
    float output = deEmphasis.processSample(biased);

    // High frequency rolloff (tape head loss)
    if (profile.highFreqRolloff < 20000.0f)
    {
        // Simple one-pole lowpass (properly scaled for sample rate)
        // Convert frequency to normalized coefficient (0-1)
        float freq = juce::jmin(profile.highFreqRolloff, 20000.0f);
        constexpr float PI = 3.14159265358979323846f;
        float omega = 2.0f * PI * freq / (float)sampleRate;
        // Clamp omega to safe range to avoid tan() domain errors
        omega = juce::jlimit(0.001f, PI - 0.001f, omega);
        float alpha = 1.0f / (1.0f + 1.0f/std::tan(omega * 0.5f));

        lpState = output * alpha + lpState * (1.0f - alpha);

        // Add denormal protection
        if (std::abs(lpState) < 1e-10f)
            lpState = 0.0f;

        output = lpState;
    }

    return output;
}

//==============================================================================
// TubeEmulation Implementation
//==============================================================================

void HardwareSaturation::TubeEmulation::prepare(double sampleRate)
{
    millerCapState = 0.0f;
    gridCurrent = 0.0f;
}

void HardwareSaturation::TubeEmulation::reset()
{
    millerCapState = 0.0f;
    gridCurrent = 0.0f;
}

float HardwareSaturation::TubeEmulation::process(float input, const HarmonicProfile& profile)
{
    // Choose between triode (smooth) and pentode (aggressive) based on profile
    float processed;

    if (profile.evenOddRatio > 0.6f)
    {
        // More even harmonics -> use triode
        processed = processTriode(input);
    }
    else
    {
        // More odd harmonics -> use pentode
        processed = processPentode(input);
    }

    // Grid current modeling (compression effect)
    if (input > 0.5f)
    {
        gridCurrent = (input - 0.5f) * 0.2f;
        processed -= gridCurrent;  // Compression effect
    }
    else
    {
        gridCurrent *= 0.95f;  // Slow release
    }

    // Apply DC-blocking to grid current to prevent accumulation
    gridCurrent *= 0.9998f;

    // Miller capacitance (frequency-dependent)
    float hfContent = input - millerCapState;
    millerCapState *= 0.9999f;  // DC blocking to prevent accumulation
    millerCapState += hfContent * 0.3f;
    processed -= hfContent * 0.1f;  // Reduces highs slightly

    return processed;
}

float HardwareSaturation::TubeEmulation::processTriode(float input)
{
    // Triode characteristic curve (asymmetric, even harmonics)
    // Approximation of 12AX7 plate curve
    float x = input * 1.5f;

    if (x >= 0)
        return x / (1.0f + std::abs(x));  // Soft saturation on positive
    else
        return x / (1.0f + std::abs(x) * 1.3f);  // Harder on negative (asymmetric)
}

float HardwareSaturation::TubeEmulation::processPentode(float input)
{
    // Pentode characteristic (more linear, odd harmonics)
    // Approximation of EF86 pentode
    float x = input * 2.0f;
    return std::tanh(x) * 0.9f;
}

//==============================================================================
// TransistorEmulation Implementation
//==============================================================================

void HardwareSaturation::TransistorEmulation::prepare(double newSampleRate)
{
    previousOutput = 0.0f;
    slewRateLimit = 10.0f;  // Realistic slew rate for audio op-amps
    sampleRate = newSampleRate;
}

void HardwareSaturation::TransistorEmulation::reset()
{
    previousOutput = 0.0f;
}

float HardwareSaturation::TransistorEmulation::process(float input, const HarmonicProfile& profile)
{
    float processed = input;

    // Crossover distortion (Class AB characteristic)
    if (std::abs(input) < crossoverDistortion)
    {
        // Dead zone near zero crossing
        processed *= 0.8f;
    }

    // Transistor saturation (harder than tubes)
    processed = std::tanh(processed * 2.5f) * 0.8f;

    // Slew rate limiting (creates high-frequency distortion)
    float delta = processed - previousOutput;
    // Convert slew rate from V/μs to max change per sample
    // At 48kHz: period = 1/48000 s = 20.833μs
    // Max change = 10 V/μs × 20.833μs = 208.33 V/sample
    float maxDelta = (slewRateLimit * 1e6f) / (float)sampleRate;

    if (std::abs(delta) > maxDelta)
    {
        processed = previousOutput + (delta >= 0 ? maxDelta : -maxDelta);
    }

    previousOutput = processed;

    // Asymmetric clipping for Class A circuits
    if (profile.asymmetric)
    {
        if (processed > 0.7f)
            processed = 0.7f + (processed - 0.7f) * 0.2f;
        else if (processed < -0.85f)
            processed = -0.85f + (processed + 0.85f) * 0.1f;
    }

    return processed;
}

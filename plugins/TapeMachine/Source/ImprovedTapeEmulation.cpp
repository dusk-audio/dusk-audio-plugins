#include "ImprovedTapeEmulation.h"

ImprovedTapeEmulation::ImprovedTapeEmulation()
{
    reset();
}

void ImprovedTapeEmulation::prepare(double sampleRate, int samplesPerBlock)
{
    if (sampleRate <= 0.0) sampleRate = 44100.0;
    if (samplesPerBlock <= 0) samplesPerBlock = 512;

    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // Prepare wow/flutter processor with correct buffer size
    wowFlutter.prepare(sampleRate);

    reset();

    // Initialize all filters with default coefficients for 15 IPS NAB
    auto nyquist = sampleRate * 0.5;

    // Default NAB Pre-emphasis for 15 IPS (recording EQ - boosts highs)
    // 50μs time constant = 3183 Hz corner frequency
    preEmphasisFilter1.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 3183.0f, 0.707f, juce::Decibels::decibelsToGain(6.0f)); // +6dB HF boost at 10kHz

    preEmphasisFilter2.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, 10000.0f, 2.0f, juce::Decibels::decibelsToGain(1.5f)); // Gentle HF lift

    // Default NAB De-emphasis for 15 IPS (playback EQ - restores flat response)
    // 3180μs time constant = 50 Hz corner frequency for LF boost
    // 50μs time constant = 3183 Hz corner frequency for HF cut
    deEmphasisFilter1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sampleRate, 50.0f, 0.707f, juce::Decibels::decibelsToGain(3.0f)); // +3dB LF restoration

    deEmphasisFilter2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 3183.0f, 0.707f, juce::Decibels::decibelsToGain(-6.0f)); // -6dB HF restoration

    // Head bump (characteristic low-frequency resonance)
    headBumpFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, 60.0f, 1.5f, juce::Decibels::decibelsToGain(3.0f)); // +3dB bump at 60Hz

    // HF loss filters (tape self-erasure and spacing loss)
    hfLossFilter1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, 16000.0f, 0.707f);

    hfLossFilter2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 10000.0f, 0.5f, juce::Decibels::decibelsToGain(-2.0f));

    // Gap loss (playback head gap effect)
    gapLossFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 12000.0f, 0.707f, juce::Decibels::decibelsToGain(-1.5f));

    // Bias filter (HF boost from bias current)
    biasFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 8000.0f, 0.707f, juce::Decibels::decibelsToGain(2.0f));

    // Noise generator pinking filter
    noiseGen.pinkingFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, 3000.0f, 0.7f);

    // DC blocking filter - essential to prevent subsonic rumble
    dcBlocker.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, 20.0f, 0.707f);  // Cut below 20Hz

    // Saturation envelope followers
    saturator.updateCoefficients(0.1f, 10.0f, sampleRate);
}

void ImprovedTapeEmulation::reset()
{
    preEmphasisFilter1.reset();
    preEmphasisFilter2.reset();
    deEmphasisFilter1.reset();
    deEmphasisFilter2.reset();
    headBumpFilter.reset();
    hfLossFilter1.reset();
    hfLossFilter2.reset();
    gapLossFilter.reset();
    biasFilter.reset();
    noiseGen.pinkingFilter.reset();

    hysteresisProc.state = 0.0f;
    hysteresisProc.previousInput = 0.0f;
    hysteresisProc.previousOutput = 0.0f;

    saturator.envelope = 0.0f;

    dcBlocker.reset();

    if (!wowFlutter.delayBuffer.empty())
    {
        std::fill(wowFlutter.delayBuffer.begin(), wowFlutter.delayBuffer.end(), 0.0f);
    }
    wowFlutter.writeIndex = 0;
    wowFlutter.wowPhase = 0.0;
    wowFlutter.flutterPhase = 0.0;

    crosstalkBuffer = 0.0f;
}

ImprovedTapeEmulation::MachineCharacteristics
ImprovedTapeEmulation::getMachineCharacteristics(TapeMachine machine)
{
    MachineCharacteristics chars;

    switch (machine)
    {
        case Swiss800:
            // Swiss 800: Clean, minimal coloration, excellent transient response
            chars.headBumpFreq = 50.0f;
            chars.headBumpGain = 2.5f;  // Subtle head bump
            chars.headBumpQ = 0.8f;

            chars.hfRolloffFreq = 20000.0f;  // Extended HF
            chars.hfRolloffSlope = -12.0f;

            chars.saturationKnee = 0.85f;  // Hard knee (clean)
            // Swiss precision harmonics - balanced, clean with slight even-order emphasis
            chars.saturationHarmonics[0] = 0.20f; // 2nd harmonic (subtle warmth)
            chars.saturationHarmonics[1] = 0.08f; // 3rd harmonic (minimal edge)
            chars.saturationHarmonics[2] = 0.10f; // 4th harmonic (smoothness)
            chars.saturationHarmonics[3] = 0.03f; // 5th harmonic
            chars.saturationHarmonics[4] = 0.05f; // 6th harmonic

            chars.compressionRatio = 0.08f;  // Subtle compression
            chars.compressionAttack = 0.1f;
            chars.compressionRelease = 50.0f;

            chars.phaseShift = 0.02f;
            chars.crosstalkAmount = -65.0f;  // Excellent channel separation
            break;

        case Classic102:
            // Classic 102: Warmer, more colored, classic American sound
            chars.headBumpFreq = 60.0f;
            chars.headBumpGain = 4.0f;  // More pronounced head bump
            chars.headBumpQ = 1.2f;

            chars.hfRolloffFreq = 18000.0f;
            chars.hfRolloffSlope = -18.0f;

            chars.saturationKnee = 0.7f;   // Softer knee (warmer)
            // Classic American harmonics - strong 2nd and 3rd for warmth and punch
            chars.saturationHarmonics[0] = 0.35f; // 2nd harmonic (lots of warmth)
            chars.saturationHarmonics[1] = 0.15f; // 3rd harmonic (punch/presence)
            chars.saturationHarmonics[2] = 0.08f; // 4th harmonic
            chars.saturationHarmonics[3] = 0.05f; // 5th harmonic
            chars.saturationHarmonics[4] = 0.03f; // 6th harmonic

            chars.compressionRatio = 0.12f;  // More "glue"
            chars.compressionAttack = 0.2f;
            chars.compressionRelease = 100.0f;

            chars.phaseShift = 0.05f;
            chars.crosstalkAmount = -55.0f;  // More crosstalk (vintage)
            break;

        case Blend:
        default:
            // Blend of both machines
            chars.headBumpFreq = 55.0f;
            chars.headBumpGain = 3.25f;
            chars.headBumpQ = 1.0f;

            chars.hfRolloffFreq = 19000.0f;
            chars.hfRolloffSlope = -15.0f;

            chars.saturationKnee = 0.77f;  // Balanced
            // Balanced harmonic profile - best of both worlds
            chars.saturationHarmonics[0] = 0.27f; // 2nd harmonic (warmth)
            chars.saturationHarmonics[1] = 0.11f; // 3rd harmonic (presence)
            chars.saturationHarmonics[2] = 0.09f; // 4th harmonic
            chars.saturationHarmonics[3] = 0.04f; // 5th harmonic
            chars.saturationHarmonics[4] = 0.04f; // 6th harmonic

            chars.compressionRatio = 0.10f;  // Moderate
            chars.compressionAttack = 0.15f;
            chars.compressionRelease = 75.0f;

            chars.phaseShift = 0.035f;
            chars.crosstalkAmount = -60.0f;
            break;
    }

    return chars;
}

ImprovedTapeEmulation::TapeCharacteristics
ImprovedTapeEmulation::getTapeCharacteristics(TapeType type)
{
    TapeCharacteristics chars;

    switch (type)
    {
        case Type456:
            // Standard professional tape - industry workhorse
            chars.coercivity = 0.8f;
            chars.retentivity = 0.85f;
            chars.saturationPoint = 0.9f;

            chars.hysteresisAmount = 0.35f;  // Moderate hysteresis
            chars.hysteresisAsymmetry = 0.1f;

            chars.noiseFloor = -65.0f;
            chars.modulationNoise = 0.02f;

            chars.lfEmphasis = 1.15f;  // Slight low-end emphasis
            chars.hfLoss = 0.92f;      // Moderate HF loss
            break;

        case TypeGP9:
            // High output, low noise - modern formulation
            chars.coercivity = 0.9f;
            chars.retentivity = 0.92f;
            chars.saturationPoint = 0.95f;

            chars.hysteresisAmount = 0.25f;  // Lower hysteresis (cleaner)
            chars.hysteresisAsymmetry = 0.05f;

            chars.noiseFloor = -68.0f;  // Lower noise floor
            chars.modulationNoise = 0.015f;

            chars.lfEmphasis = 1.05f;  // Flatter response
            chars.hfLoss = 0.97f;      // Better HF retention
            break;

        case Type911:
            // European formulation - warmer character
            chars.coercivity = 0.85f;
            chars.retentivity = 0.88f;
            chars.saturationPoint = 0.88f;

            chars.hysteresisAmount = 0.40f;  // Higher hysteresis (more color)
            chars.hysteresisAsymmetry = 0.15f;

            chars.noiseFloor = -64.0f;
            chars.modulationNoise = 0.025f;

            chars.lfEmphasis = 1.20f;  // More low-end warmth
            chars.hfLoss = 0.90f;      // Softer highs
            break;

        case Type250:
            // Classic vintage tape - lots of character
            chars.coercivity = 0.75f;
            chars.retentivity = 0.8f;
            chars.saturationPoint = 0.85f;

            chars.hysteresisAmount = 0.45f;  // High hysteresis (vintage color)
            chars.hysteresisAsymmetry = 0.2f;  // More asymmetry

            chars.noiseFloor = -62.0f;  // Higher noise (vintage)
            chars.modulationNoise = 0.03f;

            chars.lfEmphasis = 1.25f;  // Strong low-end coloration
            chars.hfLoss = 0.88f;      // Rolled-off highs
            break;

        default:
            chars = getTapeCharacteristics(Type456);
            break;
    }

    return chars;
}

ImprovedTapeEmulation::SpeedCharacteristics
ImprovedTapeEmulation::getSpeedCharacteristics(TapeSpeed speed)
{
    SpeedCharacteristics chars;

    switch (speed)
    {
        case Speed_7_5_IPS:
            // Lower speed: more head bump, less HF, more noise
            chars.headBumpMultiplier = 1.5f;
            chars.hfExtension = 0.7f;
            chars.noiseReduction = 1.0f;
            chars.flutterRate = 3.5f;
            chars.wowRate = 0.33f;
            break;

        case Speed_15_IPS:
            // Standard speed
            chars.headBumpMultiplier = 1.0f;
            chars.hfExtension = 1.0f;
            chars.noiseReduction = 0.7f;
            chars.flutterRate = 5.0f;
            chars.wowRate = 0.5f;
            break;

        case Speed_30_IPS:
            // Higher speed: less head bump, extended HF, less noise
            chars.headBumpMultiplier = 0.7f;
            chars.hfExtension = 1.3f;
            chars.noiseReduction = 0.5f;
            chars.flutterRate = 7.0f;
            chars.wowRate = 0.8f;
            break;

        default:
            chars = getSpeedCharacteristics(Speed_15_IPS);
            break;
    }

    return chars;
}

void ImprovedTapeEmulation::updateFilters(TapeMachine machine, TapeSpeed speed,
                                         TapeType type, float biasAmount)
{
    auto machineChars = getMachineCharacteristics(machine);
    auto tapeChars = getTapeCharacteristics(type);
    auto speedChars = getSpeedCharacteristics(speed);

    // NAB EQ curves change with tape speed
    // Speed-dependent EQ time constants
    float nabLowFreq = 50.0f;    // 3180μs for all speeds
    float nabHighFreq = 3183.0f; // 50μs for 15 IPS

    // Adjust EQ based on speed (NAB standard)
    switch (speed)
    {
        case Speed_7_5_IPS:
            // 7.5 IPS: 3180μs + 90μs (1768 Hz)
            nabHighFreq = 1768.0f;
            break;
        case Speed_15_IPS:
            // 15 IPS: 3180μs + 50μs (3183 Hz)
            nabHighFreq = 3183.0f;
            break;
        case Speed_30_IPS:
            // 30 IPS: No standard, but typically 3180μs + 35μs (4547 Hz)
            nabHighFreq = 4547.0f;
            break;
    }

    // Update pre-emphasis and de-emphasis based on speed
    preEmphasisFilter1.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, nabHighFreq, 0.707f,
        juce::Decibels::decibelsToGain(speed == Speed_7_5_IPS ? 8.0f : 6.0f));

    deEmphasisFilter2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, nabHighFreq, 0.707f,
        juce::Decibels::decibelsToGain(speed == Speed_7_5_IPS ? -8.0f : -6.0f));

    // Update head bump filter based on machine and speed
    float headBumpFreq = machineChars.headBumpFreq;
    float headBumpGain = machineChars.headBumpGain * speedChars.headBumpMultiplier;

    // Head bump frequency shifts with speed
    if (speed == Speed_30_IPS)
        headBumpFreq *= 1.4f;  // Higher frequency at higher speed
    else if (speed == Speed_7_5_IPS)
        headBumpFreq *= 0.7f;  // Lower frequency at lower speed

    // Prevent head bump from going too low (causes subsonic issues)
    headBumpFreq = juce::jmax(25.0f, headBumpFreq);

    // Reduce Q to prevent oscillation at low frequencies
    float adjustedQ = juce::jmin(machineChars.headBumpQ, 0.9f);

    headBumpFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate, headBumpFreq, adjustedQ,
        juce::Decibels::decibelsToGain(headBumpGain * 0.7f));  // Reduce gain to prevent oscillation

    // Update HF loss based on tape speed and type
    float hfCutoff = machineChars.hfRolloffFreq * speedChars.hfExtension * tapeChars.hfLoss;
    hfLossFilter1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        currentSampleRate, hfCutoff, 0.707f);

    hfLossFilter2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, hfCutoff * 0.6f, 0.5f,
        juce::Decibels::decibelsToGain(-2.0f * tapeChars.hfLoss));

    // Gap loss is more pronounced at lower speeds
    float gapLossFreq = speed == Speed_7_5_IPS ? 8000.0f : (speed == Speed_30_IPS ? 15000.0f : 12000.0f);
    float gapLossAmount = speed == Speed_7_5_IPS ? -3.0f : (speed == Speed_30_IPS ? -0.5f : -1.5f);
    gapLossFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, gapLossFreq, 0.707f, juce::Decibels::decibelsToGain(gapLossAmount));

    // Update bias filter (more bias = more HF boost but also more distortion)
    float biasFreq = 6000.0f + (biasAmount * 4000.0f);
    float biasGain = juce::Decibels::decibelsToGain(biasAmount * 3.0f);
    biasFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, biasFreq, 0.707f, biasGain);

    // Update saturation based on machine characteristics
    saturator.updateCoefficients(machineChars.compressionAttack,
                                 machineChars.compressionRelease,
                                 currentSampleRate);
}

float ImprovedTapeEmulation::processSample(float input,
                                          TapeMachine machine,
                                          TapeSpeed speed,
                                          TapeType type,
                                          float biasAmount,
                                          float saturationDepth,
                                          float wowFlutterAmount,
                                          bool noiseEnabled,
                                          float noiseAmount)
{
    // Update input level metering
    inputLevel.store(std::abs(input));

    // Update filters when parameters change (using instance variables)
    if (machine != m_lastMachine || speed != m_lastSpeed || type != m_lastType || std::abs(biasAmount - m_lastBias) > 0.01f)
    {
        updateFilters(machine, speed, type, biasAmount);
        m_lastMachine = machine;
        m_lastSpeed = speed;
        m_lastType = type;
        m_lastBias = biasAmount;
    }

    // Get characteristics for current settings
    auto machineChars = getMachineCharacteristics(machine);
    auto tapeChars = getTapeCharacteristics(type);
    auto speedChars = getSpeedCharacteristics(speed);

    // Input gain staging (important for tape saturation)
    float signal = input * 0.95f;

    // 1. Pre-emphasis (recording EQ)
    signal = preEmphasisFilter1.processSample(signal);
    signal = preEmphasisFilter2.processSample(signal);

    // 2. Bias-induced HF boost
    if (biasAmount > 0.0f)
    {
        signal = biasFilter.processSample(signal);
    }

    // 3. Tape hysteresis (magnetic non-linearity)
    signal = hysteresisProc.process(signal,
                                    tapeChars.hysteresisAmount * saturationDepth,
                                    tapeChars.hysteresisAsymmetry,
                                    tapeChars.saturationPoint);

    // 4. Harmonic generation (tape saturation)
    if (saturationDepth > 0.0f)
    {
        float harmonics = generateHarmonics(signal, machineChars.saturationHarmonics, 5);
        signal = signal * (1.0f - saturationDepth * 0.3f) + harmonics * saturationDepth;
    }

    // 5. Soft saturation/compression
    signal = saturator.process(signal,
                               machineChars.saturationKnee,
                               machineChars.compressionRatio * saturationDepth,
                               1.0f);

    // 6. Head gap loss simulation
    signal = gapLossFilter.processSample(signal);

    // 7. Apply tape formulation's frequency characteristics
    // LF emphasis based on tape type
    if (tapeChars.lfEmphasis != 1.0f)
    {
        signal *= (1.0f + (tapeChars.lfEmphasis - 1.0f) * 0.5f);
    }

    // 8. HF loss (self-erasure and spacing loss) affected by tape type
    signal = hfLossFilter1.processSample(signal);
    signal = hfLossFilter2.processSample(signal);

    // 9. Head bump resonance
    signal = headBumpFilter.processSample(signal);

    // 10. Wow & Flutter
    if (wowFlutterAmount > 0.0f)
    {
        signal = wowFlutter.process(signal,
                                   wowFlutterAmount * 0.7f,  // Wow amount
                                   wowFlutterAmount * 0.3f,  // Flutter amount
                                   speedChars.wowRate,
                                   speedChars.flutterRate,
                                   currentSampleRate);
    }

    // 11. De-emphasis (playback EQ)
    signal = deEmphasisFilter1.processSample(signal);
    signal = deEmphasisFilter2.processSample(signal);

    // 12. Add tape noise (only when noise button is enabled)
    // ABSOLUTELY NO NOISE when button is off
    if (noiseEnabled)  // Only if explicitly enabled
    {
        if (noiseAmount > 0.001f)  // And amount is meaningful
        {
            float noise = noiseGen.generateNoise(
                juce::Decibels::decibelsToGain(tapeChars.noiseFloor) * speedChars.noiseReduction * noiseAmount * 0.01f,
                tapeChars.modulationNoise,
                signal);
            signal += noise * 0.05f;  // Subtle noise when enabled
        }
    }
    // NO ELSE - when disabled, absolutely no noise is added

    // 13. DC blocking disabled - main processor chain already has highpass filter
    // signal = dcBlocker.processSample(signal);

    // 14. Final soft clipping
    signal = softClip(signal, 0.95f);

    // Update output level metering
    outputLevel.store(std::abs(signal));
    gainReduction.store(std::abs(input) - std::abs(signal));

    return signal;
}

// Hysteresis processor implementation
float ImprovedTapeEmulation::HysteresisProcessor::process(float input, float amount,
                                                         float asymmetry, float saturation)
{
    // M-S hysteresis loop simulation
    float drive = 1.0f + amount * 4.0f;
    float x = input * drive;

    // Asymmetric saturation
    float sign = (x >= 0.0f) ? 1.0f : -1.0f;
    float absX = std::abs(x);
    float satX = std::tanh(absX * saturation);

    // Apply asymmetry to positive and negative sides differently
    if (sign > 0)
        satX *= (1.0f + asymmetry * 0.2f);
    else
        satX *= (1.0f - asymmetry * 0.2f);

    // Hysteresis state integration
    float delta = satX * sign - state;
    state += delta * (1.0f - amount * 0.5f);

    // Mix dry and wet based on amount
    float output = input * (1.0f - amount) + state * amount;

    // DC blocker to prevent low frequency buildup from hysteresis
    // Simple first-order high-pass at 5Hz
    const float dcBlockerCutoff = 0.9995f;  // ~5Hz at 44.1kHz
    output = output - previousInput + dcBlockerCutoff * previousOutput;

    // Update history
    previousInput = input;
    previousOutput = output;

    return output;
}

// Tape saturator implementation
void ImprovedTapeEmulation::TapeSaturator::updateCoefficients(float attackMs, float releaseMs,
                                                             double sampleRate)
{
    attackCoeff = std::exp(-1.0f / (attackMs * 0.001f * sampleRate));
    releaseCoeff = std::exp(-1.0f / (releaseMs * 0.001f * sampleRate));
}

float ImprovedTapeEmulation::TapeSaturator::process(float input, float threshold,
                                                   float ratio, float makeup)
{
    float absInput = std::abs(input);

    // Update envelope
    float targetEnv = absInput;
    float rate = (targetEnv > envelope) ? attackCoeff : releaseCoeff;
    envelope = targetEnv + (envelope - targetEnv) * rate;

    // Apply compression above threshold
    float gain = 1.0f;
    if (envelope > threshold && envelope > 0.0001f)  // Safety check for division
    {
        float excess = envelope - threshold;
        float compressedExcess = excess * (1.0f - ratio);
        gain = (threshold + compressedExcess) / envelope;
    }

    return input * gain * makeup;
}

// Wow & Flutter processor implementation
float ImprovedTapeEmulation::WowFlutterProcessor::process(float input, float wowAmount,
                                                         float flutterAmount, float wowRate,
                                                         float flutterRate, double sampleRate)
{
    // Write to delay buffer
    delayBuffer[writeIndex] = input;

    // Calculate modulation using double precision phases
    float wowMod = static_cast<float>(std::sin(wowPhase)) * wowAmount * 10.0f;  // ±10 samples max
    float flutterMod = static_cast<float>(std::sin(flutterPhase)) * flutterAmount * 2.0f;  // ±2 samples max
    float randomMod = dist(rng) * flutterAmount * 0.5f;  // Random component

    float totalDelay = 20.0f + wowMod + flutterMod + randomMod;  // Base delay + modulation

    // Fractional delay interpolation
    int delaySamples = static_cast<int>(totalDelay);
    float fraction = totalDelay - delaySamples;

    int readIndex1 = (writeIndex - delaySamples + delayBuffer.size()) % delayBuffer.size();
    int readIndex2 = (readIndex1 - 1 + delayBuffer.size()) % delayBuffer.size();

    float sample1 = delayBuffer[readIndex1];
    float sample2 = delayBuffer[readIndex2];

    // Linear interpolation
    float output = sample1 * (1.0f - fraction) + sample2 * fraction;

    // Update phases with double precision
    double wowIncrement = 2.0 * juce::MathConstants<double>::pi * wowRate / sampleRate;
    double flutterIncrement = 2.0 * juce::MathConstants<double>::pi * flutterRate / sampleRate;

    wowPhase += wowIncrement;
    if (wowPhase > 2.0 * juce::MathConstants<double>::pi)
        wowPhase -= 2.0 * juce::MathConstants<double>::pi;

    flutterPhase += flutterIncrement;
    if (flutterPhase > 2.0 * juce::MathConstants<double>::pi)
        flutterPhase -= 2.0 * juce::MathConstants<double>::pi;

    // Update write index
    writeIndex = (writeIndex + 1) % delayBuffer.size();

    return output;
}

// Noise generator implementation
float ImprovedTapeEmulation::NoiseGenerator::generateNoise(float noiseFloor,
                                                          float modulationAmount,
                                                          float signal)
{
    // Generate white noise
    float whiteNoise = gaussian(rng) * noiseFloor;

    // Pink it
    float pinkNoise = pinkingFilter.processSample(whiteNoise);

    // Modulate with signal envelope
    float envelope = std::abs(signal);
    float modulation = 1.0f + envelope * modulationAmount;

    return pinkNoise * modulation;
}

// Soft clipping function
float ImprovedTapeEmulation::softClip(float input, float threshold)
{
    float absInput = std::abs(input);
    if (absInput < threshold)
        return input;

    float sign = (input >= 0.0f) ? 1.0f : -1.0f;
    float excess = absInput - threshold;
    float clipped = threshold + std::tanh(excess * 3.0f) / 3.0f;

    return clipped * sign;
}

// Harmonic generator using waveshaping
float ImprovedTapeEmulation::generateHarmonics(float input, const float* harmonicProfile,
                                              int numHarmonics)
{
    // Chebyshev polynomials for harmonic generation
    // T0(x) = 1
    // T1(x) = x
    // T2(x) = 2x^2 - 1 (generates 2nd harmonic)
    // T3(x) = 4x^3 - 3x (generates 3rd harmonic)
    // T4(x) = 8x^4 - 8x^2 + 1 (generates 4th harmonic)
    // T5(x) = 16x^5 - 20x^3 + 5x (generates 5th harmonic)
    // T6(x) = 32x^6 - 48x^4 + 18x^2 - 1 (generates 6th harmonic)

    // Soft-clip input to [-1, 1] range for Chebyshev polynomials
    float x = std::tanh(input * 0.7f);
    float x2 = x * x;
    float x3 = x2 * x;
    float x4 = x2 * x2;
    float x5 = x3 * x2;
    float x6 = x3 * x3;

    float output = input;  // Start with fundamental

    if (numHarmonics > 0 && harmonicProfile[0] > 0.0f) {
        // 2nd harmonic (even - warmth)
        float h2 = (2.0f * x2 - 1.0f) * harmonicProfile[0];
        output += h2 * 0.5f;
    }

    if (numHarmonics > 1 && harmonicProfile[1] > 0.0f) {
        // 3rd harmonic (odd - edge/presence)
        float h3 = (4.0f * x3 - 3.0f * x) * harmonicProfile[1];
        output += h3 * 0.3f;
    }

    if (numHarmonics > 2 && harmonicProfile[2] > 0.0f) {
        // 4th harmonic (even - smoothness)
        float h4 = (8.0f * x4 - 8.0f * x2 + 1.0f) * harmonicProfile[2];
        output += h4 * 0.25f;
    }

    if (numHarmonics > 3 && harmonicProfile[3] > 0.0f) {
        // 5th harmonic (odd - bite)
        float h5 = (16.0f * x5 - 20.0f * x3 + 5.0f * x) * harmonicProfile[3];
        output += h5 * 0.2f;
    }

    if (numHarmonics > 4 && harmonicProfile[4] > 0.0f) {
        // 6th harmonic (even - air)
        float h6 = (32.0f * x6 - 48.0f * x4 + 18.0f * x2 - 1.0f) * harmonicProfile[4];
        output += h6 * 0.15f;
    }

    return output;
}
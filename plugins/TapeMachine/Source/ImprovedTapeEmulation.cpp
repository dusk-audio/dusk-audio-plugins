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

    // Prepare per-channel wow/flutter delay line
    perChannelWowFlutter.prepare(sampleRate);

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

    if (!perChannelWowFlutter.delayBuffer.empty())
    {
        std::fill(perChannelWowFlutter.delayBuffer.begin(), perChannelWowFlutter.delayBuffer.end(), 0.0f);
    }
    perChannelWowFlutter.writeIndex = 0;

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
            // Swiss precision harmonics - very subtle, only when driven hard
            chars.saturationHarmonics[0] = 0.06f; // 2nd harmonic (subtle warmth)
            chars.saturationHarmonics[1] = 0.02f; // 3rd harmonic (minimal edge)
            chars.saturationHarmonics[2] = 0.03f; // 4th harmonic (smoothness)
            chars.saturationHarmonics[3] = 0.01f; // 5th harmonic
            chars.saturationHarmonics[4] = 0.015f; // 6th harmonic

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
            // Classic American harmonics - more warmth when driven, but still subtle
            chars.saturationHarmonics[0] = 0.12f; // 2nd harmonic (warmth)
            chars.saturationHarmonics[1] = 0.05f; // 3rd harmonic (punch/presence)
            chars.saturationHarmonics[2] = 0.03f; // 4th harmonic
            chars.saturationHarmonics[3] = 0.018f; // 5th harmonic
            chars.saturationHarmonics[4] = 0.012f; // 6th harmonic

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
            chars.saturationHarmonics[0] = 0.09f; // 2nd harmonic (warmth)
            chars.saturationHarmonics[1] = 0.035f; // 3rd harmonic (presence)
            chars.saturationHarmonics[2] = 0.03f; // 4th harmonic
            chars.saturationHarmonics[3] = 0.015f; // 5th harmonic
            chars.saturationHarmonics[4] = 0.013f; // 6th harmonic

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

            chars.hysteresisAmount = 0.18f;  // Moderate hysteresis (reduced)
            chars.hysteresisAsymmetry = 0.05f;

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

            chars.hysteresisAmount = 0.12f;  // Lower hysteresis (cleaner, reduced)
            chars.hysteresisAsymmetry = 0.03f;

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

            chars.hysteresisAmount = 0.22f;  // Higher hysteresis (more color, reduced)
            chars.hysteresisAsymmetry = 0.08f;

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

            chars.hysteresisAmount = 0.28f;  // High hysteresis (vintage color, reduced)
            chars.hysteresisAsymmetry = 0.10f;  // More asymmetry

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

    // NAB/IEC EQ curves - UAD-accurate implementation
    // NAB (American): Used by Ampex - more HF boost/cut
    // IEC/CCIR (European): Used by Studer - gentler curves

    float preEmphasisFreq = 3183.0f;   // 50μs time constant
    float preEmphasisGain = 6.0f;       // dB
    float deEmphasisFreq = 3183.0f;
    float deEmphasisGain = -6.0f;       // dB
    float lowFreqCompensation = 50.0f;  // 3180μs

    // Speed-dependent EQ adjustments (UAD-accurate)
    switch (speed)
    {
        case Speed_7_5_IPS:
            // 7.5 IPS: 90μs = 1768 Hz, more pre-emphasis needed
            preEmphasisFreq = 1768.0f;
            preEmphasisGain = 9.0f;  // More boost at low speed
            deEmphasisFreq = 1768.0f;
            deEmphasisGain = -9.0f;
            break;
        case Speed_15_IPS:
            // 15 IPS: 50μs = 3183 Hz (reference speed)
            preEmphasisFreq = 3183.0f;
            preEmphasisGain = 6.0f;
            deEmphasisFreq = 3183.0f;
            deEmphasisGain = -6.0f;
            break;
        case Speed_30_IPS:
            // 30 IPS: 35μs = 4547 Hz, extended HF response
            preEmphasisFreq = 4547.0f;
            preEmphasisGain = 4.5f;  // Less boost at high speed
            deEmphasisFreq = 4547.0f;
            deEmphasisGain = -4.5f;
            break;
    }

    // Machine-specific EQ characteristics
    if (machine == Swiss800)
    {
        // IEC/CCIR curves - slightly different than NAB
        preEmphasisGain *= 0.9f;   // Gentler
        deEmphasisGain *= 0.9f;
    }
    else if (machine == Classic102)
    {
        // Pure NAB curves - more pronounced
        preEmphasisGain *= 1.1f;
        deEmphasisGain *= 1.1f;
    }

    // Update pre-emphasis (recording EQ)
    preEmphasisFilter1.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, preEmphasisFreq, 0.707f,
        juce::Decibels::decibelsToGain(preEmphasisGain));

    // Add subtle mid-range presence boost (UAD characteristic)
    preEmphasisFilter2.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate, preEmphasisFreq * 2.5f, 1.5f,
        juce::Decibels::decibelsToGain(1.2f));

    // Update de-emphasis (playback EQ) - compensates for pre-emphasis
    deEmphasisFilter1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, lowFreqCompensation, 0.707f,
        juce::Decibels::decibelsToGain(2.5f)); // LF restoration

    deEmphasisFilter2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, deEmphasisFreq, 0.707f,
        juce::Decibels::decibelsToGain(deEmphasisGain));

    // Update head bump filter - UAD-accurate scaling
    // Head bump is caused by magnetic flux leakage and varies with speed/machine
    float headBumpFreq = machineChars.headBumpFreq;
    float headBumpGain = machineChars.headBumpGain * speedChars.headBumpMultiplier;
    float headBumpQ = machineChars.headBumpQ;

    // Speed-dependent head bump frequency (research-based)
    // At higher speeds, tape moves faster past the head, shifting resonance up
    switch (speed)
    {
        case Speed_7_5_IPS:
            // Lower speed: more pronounced bump at lower freq
            headBumpFreq = machineChars.headBumpFreq * 0.65f;  // ~35-40 Hz
            headBumpGain *= 1.4f;  // More pronounced
            headBumpQ *= 1.3f;     // Sharper peak
            break;
        case Speed_15_IPS:
            // Reference speed
            headBumpFreq = machineChars.headBumpFreq;  // ~50-60 Hz
            // Gain and Q use machine defaults
            break;
        case Speed_30_IPS:
            // Higher speed: less bump, higher freq
            headBumpFreq = machineChars.headBumpFreq * 1.5f;  // ~75-90 Hz
            headBumpGain *= 0.7f;  // Less pronounced
            headBumpQ *= 0.8f;     // Broader
            break;
    }

    // Tape type affects head bump (more output = more flux = more bump)
    headBumpGain *= tapeChars.lfEmphasis * 0.8f;

    // Safety limits
    headBumpFreq = juce::jlimit(30.0f, 120.0f, headBumpFreq);
    headBumpQ = juce::jlimit(0.7f, 2.0f, headBumpQ);
    headBumpGain = juce::jlimit(1.5f, 5.0f, headBumpGain);

    headBumpFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate, headBumpFreq, headBumpQ,
        juce::Decibels::decibelsToGain(headBumpGain));

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
                                          float noiseAmount,
                                          float* sharedWowFlutterMod,
                                          float calibrationLevel)
{
    // Denormal protection at input
    if (std::abs(input) < denormalPrevention)
        return 0.0f;

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

    // Calibration level affects input gain staging and saturation threshold
    // Higher calibration = more headroom = tape saturates at higher input levels
    // UAD: 0dB (nominal), +3dB, +6dB, +9dB (maximum headroom)
    float calibrationGain = juce::Decibels::decibelsToGain(calibrationLevel);

    // Input gain staging (important for tape saturation)
    // Higher calibration reduces effective input level, increasing headroom
    float signal = input * 0.95f / calibrationGain;

    // 1. Pre-emphasis (recording EQ)
    signal = preEmphasisFilter1.processSample(signal);
    signal = preEmphasisFilter2.processSample(signal);

    // 2. Bias-induced HF boost
    if (biasAmount > 0.0f)
    {
        signal = biasFilter.processSample(signal);
    }

    // Calculate input level for level-dependent processing
    // 0 VU ≈ -12dBFS = 0.25 linear
    // Tape should be clean below 0 VU and saturate progressively above it
    float inputLevel = std::abs(signal);
    const float zeroVU = 0.25f;  // -12dBFS reference level

    // Level-dependent saturation amount: 0 below threshold, increases above
    // This makes tape nearly transparent at low levels
    float levelAboveThreshold = std::max(0.0f, (inputLevel - zeroVU) / (1.0f - zeroVU));
    float levelDependentSat = levelAboveThreshold * saturationDepth;

    // 3. Tape hysteresis (magnetic non-linearity) - level dependent
    // Hysteresis is minimal at low levels, increases when tape is driven
    float hysteresisDepth = tapeChars.hysteresisAmount * levelDependentSat * 0.8f;
    signal = hysteresisProc.process(signal,
                                    hysteresisDepth,
                                    tapeChars.hysteresisAsymmetry,
                                    tapeChars.saturationPoint);

    // 4. Harmonic generation (tape saturation) - level dependent
    // Only generate significant harmonics when tape is being driven hard
    if (levelDependentSat > 0.01f)
    {
        float harmonics = generateHarmonics(signal, machineChars.saturationHarmonics, 5);
        // Mix in harmonics proportionally to how hard we're driving the tape
        signal = signal * (1.0f - levelDependentSat * 0.15f) + harmonics * levelDependentSat * 2.0f;
    }

    // 5. Soft saturation/compression
    // Calibration affects saturation threshold (higher cal = higher threshold)
    float adjustedKnee = machineChars.saturationKnee * calibrationGain;
    float makeupGain = calibrationGain;  // Compensate for input level reduction
    signal = saturator.process(signal,
                               adjustedKnee,
                               machineChars.compressionRatio * levelDependentSat,
                               makeupGain);

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

    // 10. Wow & Flutter - use shared modulation if provided for stereo coherence
    if (wowFlutterAmount > 0.0f)
    {
        if (sharedWowFlutterMod != nullptr)
        {
            // Use pre-calculated shared modulation for stereo coherence
            signal = perChannelWowFlutter.processSample(signal, *sharedWowFlutterMod);
        }
        else
        {
            // Fallback: calculate own modulation (mono or legacy behavior)
            auto speedCharsForWow = getSpeedCharacteristics(speed);
            float modulation = perChannelWowFlutter.calculateModulation(
                wowFlutterAmount * 0.7f,  // Wow amount
                wowFlutterAmount * 0.3f,  // Flutter amount
                speedCharsForWow.wowRate,
                speedCharsForWow.flutterRate,
                currentSampleRate);
            signal = perChannelWowFlutter.processSample(signal, modulation);
        }
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

    // Denormal protection at output
    if (std::abs(signal) < denormalPrevention)
        signal = 0.0f;

    // Update output level metering
    outputLevel.store(std::abs(signal));
    gainReduction.store(std::abs(input) - std::abs(signal));

    return signal;
}

// Hysteresis processor implementation - Physics-based Jiles-Atherton inspired
float ImprovedTapeEmulation::HysteresisProcessor::process(float input, float amount,
                                                         float asymmetry, float saturation)
{
    // Denormal protection
    if (std::abs(input) < 1e-8f)
        return 0.0f;

    // Physics-based parameters (simplified Jiles-Atherton model)
    // Ms: Saturation magnetization, a: domain coupling, c: reversibility
    const float Ms = saturation;              // Saturation level (tape-dependent)
    const float a = 0.01f + amount * 0.05f;   // Domain coupling strength
    const float c = 0.1f + amount * 0.2f;     // Reversible/irreversible ratio
    const float k = 0.5f + asymmetry * 0.3f;  // Coercivity (asymmetry factor)

    // Input field strength
    float H = input * (1.0f + amount * 3.0f);

    // Anhysteretic magnetization (ideal, no losses)
    // Langevin function approximation: M_an = Ms * tanh(H/a)
    float M_an = Ms * std::tanh(H / (a + 1e-6f));

    // Differential susceptibility (rate of magnetization change)
    float dM_an = Ms / (a + 1e-6f) / std::cosh(H / (a + 1e-6f));
    dM_an = dM_an * dM_an;  // Square for proper scaling

    // Direction of field change
    float dH = H - previousInput;
    float sign_dH = (dH >= 0.0f) ? 1.0f : -1.0f;

    // Irreversible magnetization component (creates hysteresis loop)
    // This is the key to magnetic memory
    float M_irr_delta = (M_an - state) / (k * sign_dH + 1e-6f);

    // Total magnetization change (reversible + irreversible)
    float dM = c * dM_an * dH + (1.0f - c) * M_irr_delta * std::abs(dH);

    // Update magnetic state with integration
    state += dM * amount;

    // Apply saturation limits to prevent runaway
    state = juce::jlimit(-Ms, Ms, state);

    // Apply asymmetry (different saturation for positive/negative)
    float asymmetryFactor = 1.0f + asymmetry * 0.15f;
    float output = state;
    if (output > 0.0f)
        output *= asymmetryFactor;
    else
        output /= asymmetryFactor;

    // Mix dry and processed
    output = input * (1.0f - amount * 0.8f) + output * amount;

    // DC blocker to prevent low frequency buildup from hysteresis
    // Simple first-order high-pass at 5Hz
    const float dcBlockerCutoff = 0.9995f;  // ~5Hz at 44.1kHz
    float preFilteredSample = output;
    output = output - previousOutput + dcBlockerCutoff * (output + previousOutput);
    output *= 0.5f;  // Compensate for doubling

    // Update history
    previousInput = H;  // Store field strength for next iteration
    previousOutput = preFilteredSample;

    return output;
}

// Tape saturator implementation
void ImprovedTapeEmulation::TapeSaturator::updateCoefficients(float attackMs, float releaseMs,
                                                             double sampleRate)
{
    // Protect against division by zero and invalid sample rates
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    // Ensure attack/release times are positive
    attackMs = std::max(0.001f, attackMs);
    releaseMs = std::max(0.001f, releaseMs);

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

// Wow & Flutter processor implementation - moved to header as inline methods

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
#include "ImprovedTapeEmulation.h"

//==============================================================================
// TransformerSaturation - Input/Output transformer coloration
//==============================================================================
void TransformerSaturation::prepare(double sampleRate)
{
    // DC blocking coefficient - ~10Hz cutoff
    dcBlockCoeff = 1.0f - (20.0f * juce::MathConstants<float>::pi / static_cast<float>(sampleRate));
    reset();
}

void TransformerSaturation::reset()
{
    dcState = 0.0f;
    hystState = 0.0f;
    prevInput = 0.0f;
    lfResonanceState = 0.0f;
}

float TransformerSaturation::process(float input, float driveAmount, bool isOutputStage)
{
    // Transformer characteristics differ between input and output stages
    // Input: More linear, subtle coloration
    // Output: More saturation, LF resonance from core

    float signal = input;

    // DC blocking (transformer coupling)
    float dcBlocked = signal - dcState;
    dcState = signal * (1.0f - dcBlockCoeff) + dcState * dcBlockCoeff;
    signal = dcBlocked;

    // Transformer core saturation - soft saturation with asymmetric harmonics
    // Real transformers have iron core hysteresis creating even harmonics
    float absSignal = std::abs(signal);
    float saturationThreshold = isOutputStage ? 0.6f : 0.8f;

    if (absSignal > saturationThreshold)
    {
        float excess = absSignal - saturationThreshold;
        // Use cubic soft clip instead of tanh to limit harmonic generation
        // Cubic: x - x^3/3 is bandlimited (only generates 3rd harmonic)
        float normalized = juce::jlimit(0.0f, 1.0f, excess * driveAmount * 3.0f);
        float satAmount = (normalized - normalized * normalized * normalized / 3.0f) / 3.0f;
        signal = (signal >= 0.0f ? 1.0f : -1.0f) * (saturationThreshold + satAmount);
    }

    // Add subtle even harmonics (transformer characteristic)
    // 2nd harmonic from core asymmetry
    float harmonic2 = signal * signal * 0.05f * driveAmount;
    // 4th harmonic (smaller)
    float harmonic4 = signal * signal * signal * signal * 0.01f * driveAmount;

    signal += harmonic2 + harmonic4;

    // Output transformer: LF resonance from core inductance (~40-60Hz)
    if (isOutputStage)
    {
        // Simple resonance using state variable
        float resonanceFreq = 0.002f;  // ~50Hz at 44.1kHz
        float resonanceQ = 0.3f;
        lfResonanceState += (signal - lfResonanceState) * resonanceFreq;
        signal += lfResonanceState * resonanceQ * driveAmount;
    }

    // Subtle transformer hysteresis (magnetic memory)
    float hystAmount = isOutputStage ? 0.02f : 0.01f;
    float hystDelta = signal - prevInput;
    hystState = hystState * 0.99f + hystDelta * hystAmount;
    signal += hystState;
    prevInput = signal;

    return signal;
}

//==============================================================================
// PlaybackHeadResponse - Repro head frequency characteristics
//==============================================================================
void PlaybackHeadResponse::prepare(double sampleRate)
{
    currentSampleRate = sampleRate;
    reset();
}

void PlaybackHeadResponse::reset()
{
    std::fill(gapDelayLine.begin(), gapDelayLine.end(), 0.0f);
    gapDelayIndex = 0;
    resonanceState1 = 0.0f;
    resonanceState2 = 0.0f;
}

float PlaybackHeadResponse::process(float input, float gapWidth, float speed)
{
    // Head gap loss - creates comb filter effect at high frequencies
    // Gap width in microns: Studer ~2.5μm, Ampex ~3.5μm
    // First null frequency = tape speed / (2 * gap width)

    // Calculate delay in samples based on gap width and speed
    // 15 IPS = 38.1 cm/s, 2.5μm gap -> null at ~76kHz (above audio, but affects HF)
    float speedCmPerSec = speed == 0 ? 19.05f : (speed == 1 ? 38.1f : 76.2f);
    float gapMicrons = gapWidth;  // 2.5 to 4.0 microns typical

    // This creates subtle HF phase shifts and filtering
    float delayMs = (gapMicrons * 0.0001f) / speedCmPerSec * 1000.0f;
    float delaySamples = delayMs * 0.001f * static_cast<float>(currentSampleRate);

    // Clamp to delay line size
    delaySamples = std::min(delaySamples, static_cast<float>(gapDelayLine.size() - 1));

    // Write to delay line
    gapDelayLine[static_cast<size_t>(gapDelayIndex)] = input;

    // Read with interpolation
    int readIndex = (gapDelayIndex - static_cast<int>(delaySamples) + static_cast<int>(gapDelayLine.size())) % static_cast<int>(gapDelayLine.size());
    float delayedSignal = gapDelayLine[static_cast<size_t>(readIndex)];

    gapDelayIndex = (gapDelayIndex + 1) % static_cast<int>(gapDelayLine.size());

    // Mix direct and delayed for comb effect (subtle)
    float gapEffect = input * 0.98f + delayedSignal * 0.02f;

    // Head resonance - mechanical resonance around 15-20kHz
    // Creates slight boost before rolloff (Studer characteristic)
    float resonanceCoeff = 0.1f;
    resonanceState1 += (gapEffect - resonanceState1) * resonanceCoeff;
    resonanceState2 += (resonanceState1 - resonanceState2) * resonanceCoeff;

    // Slight boost at resonance frequency
    float resonanceBoost = (resonanceState1 - resonanceState2) * 0.15f;
    float output = gapEffect + resonanceBoost;

    return output;
}

//==============================================================================
// BiasOscillator - Record head AC bias effects
//==============================================================================
void BiasOscillator::prepare(double sr)
{
    sampleRate = sr;
    reset();
}

void BiasOscillator::reset()
{
    phase = 0.0;
    imState = 0.0f;
}

float BiasOscillator::process(float input, float biasFreq, float biasAmount)
{
    // AC bias oscillator runs at ~100kHz (well above audio)
    // Effects on audio: improved linearity, reduced distortion, slight HF boost
    juce::ignoreUnused(biasFreq);  // Reserved for future use

    // We don't model the actual 100kHz oscillator (would alias)
    // Instead, model its effect on the audio signal:
    // 1. Reduces low-level distortion (linearizes hysteresis)
    // 2. Creates slight HF emphasis
    // 3. Can cause intermodulation at very high levels

    // Bias linearization effect - reduces distortion at low levels
    float absInput = std::abs(input);
    float linearizationFactor = 1.0f - (biasAmount * 0.3f * std::exp(-absInput * 10.0f));

    float signal = input * linearizationFactor;

    // HF emphasis from bias (already modeled in biasFilter, but add subtle effect)
    // High bias = more HF, but also more noise and potential IM distortion

    // At very high input levels, bias can cause intermodulation
    if (absInput > 0.8f && biasAmount > 0.5f)
    {
        float imAmount = (absInput - 0.8f) * biasAmount * 0.05f;
        // Simple IM approximation - creates sum/difference frequencies
        imState = imState * 0.9f + signal * imAmount;
        signal += imState * 0.1f;
    }

    return signal;
}

//==============================================================================
// MotorFlutter - Capstan and transport mechanism flutter
//==============================================================================
void MotorFlutter::prepare(double sr)
{
    sampleRate = sr;
    reset();
}

void MotorFlutter::reset()
{
    phase1 = 0.0;
    phase2 = 0.0;
    phase3 = 0.0;
}

float MotorFlutter::calculateFlutter(float motorQuality)
{
    // Motor quality: 0 = perfect (Swiss), 1 = vintage (more flutter)
    // Real machines have multiple flutter sources:
    // 1. Capstan motor rotation (~30-60Hz depending on motor poles)
    // 2. Capstan bearing noise (~10-20Hz)
    // 3. Capstan eccentricity (~2-5Hz, slower than tape wow)

    // Studer: Very low flutter, tight tolerances
    // Ampex: Slightly more character, vintage motors

    float motorFreq = 50.0f;     // 50Hz motor (Europe) or 60Hz (US)
    float bearingFreq = 15.0f;   // Bearing noise
    float eccentricityFreq = 3.0f;  // Capstan eccentricity

    // Phase increments
    double twoPi = 2.0 * juce::MathConstants<double>::pi;
    phase1 += twoPi * motorFreq / sampleRate;
    phase2 += twoPi * bearingFreq / sampleRate;
    phase3 += twoPi * eccentricityFreq / sampleRate;

    if (phase1 > twoPi) phase1 -= twoPi;
    if (phase2 > twoPi) phase2 -= twoPi;
    if (phase3 > twoPi) phase3 -= twoPi;

    // Calculate flutter components
    // Studer: ~0.02% flutter, Ampex: ~0.04% flutter
    float baseFlutter = motorQuality * 0.0004f;  // 0.04% max

    float motorComponent = static_cast<float>(std::sin(phase1)) * baseFlutter * 0.3f;
    float bearingComponent = static_cast<float>(std::sin(phase2)) * baseFlutter * 0.5f;
    float eccentricityComponent = static_cast<float>(std::sin(phase3)) * baseFlutter * 0.2f;

    // Add random jitter (bearing imperfections)
    float randomComponent = jitter(rng) * baseFlutter * 0.1f;

    // Total flutter as pitch modulation factor (multiply with delay time)
    return motorComponent + bearingComponent + eccentricityComponent + randomComponent;
}

//==============================================================================
// ImprovedTapeEmulation
//==============================================================================
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

    // Prepare new DSP components
    inputTransformer.prepare(sampleRate);
    outputTransformer.prepare(sampleRate);
    playbackHead.prepare(sampleRate);
    biasOsc.prepare(sampleRate);
    motorFlutter.prepare(sampleRate);

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

    // Subsonic filter - authentic to real tape machines (Studer/Ampex have 20-30Hz filters)
    // Removes mechanical rumble and subsonic artifacts while preserving head bump (35Hz+)
    dcBlocker.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, 25.0f, 0.707f);  // Professional mastering standard

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

    // Reset new DSP components
    inputTransformer.reset();
    outputTransformer.reset();
    playbackHead.reset();
    biasOsc.reset();
    motorFlutter.reset();

    crosstalkBuffer = 0.0f;
}

ImprovedTapeEmulation::MachineCharacteristics
ImprovedTapeEmulation::getMachineCharacteristics(TapeMachine machine)
{
    MachineCharacteristics chars;

    switch (machine)
    {
        case Swiss800:
            // Studer A800 MkIII: Swiss precision, clean but musical
            // Known for: Tight low end, extended HF, minimal coloration at moderate levels
            // REAL SPECS: THD ~0.3% at 0VU, ~1% at +3VU, 3% at +6VU (max operating level)
            // Reference: UAD documentation - 3% THD at 10dB above 355nWb/m reference
            chars.headBumpFreq = 48.0f;      // Studer head bump is lower
            chars.headBumpGain = 3.0f;       // Moderate but tight
            chars.headBumpQ = 1.0f;          // Controlled Q

            chars.hfRolloffFreq = 22000.0f;  // Extended HF (Studer is known for this)
            chars.hfRolloffSlope = -12.0f;   // Gentle rolloff

            chars.saturationKnee = 0.85f;    // Hard knee - Studer is CLEAN until driven hard
            // Studer harmonics - MUCH lower than before to match real specs
            // Real Studer: THD ~0.3% at 0VU means harmonics are very subtle
            // At +6VU (max drive): 2nd ~0.8%, 3rd ~0.1%, 4th ~0.05%
            chars.saturationHarmonics[0] = 0.008f;  // 2nd harmonic (subtle warmth)
            chars.saturationHarmonics[1] = 0.001f;  // 3rd harmonic (very low)
            chars.saturationHarmonics[2] = 0.0005f; // 4th harmonic
            chars.saturationHarmonics[3] = 0.0002f; // 5th harmonic
            chars.saturationHarmonics[4] = 0.0001f; // 6th harmonic

            chars.compressionRatio = 0.05f;  // Very light compression until driven
            chars.compressionAttack = 0.08f; // Fast attack (Studer is responsive)
            chars.compressionRelease = 40.0f; // Quick release

            chars.phaseShift = 0.015f;       // Minimal phase issues
            chars.crosstalkAmount = -70.0f;  // Excellent channel separation (Studer spec)
            break;

        case Classic102:
            // Ampex ATR-102: Classic American warmth and punch
            // Known for: Rich low end, musical saturation, "larger than life" sound
            // REAL SPECS: THD ~0.5% at 0VU, ~1.5% at +3VU, 3% at +6VU
            // Reference: UAD - "almost totally clean or pushed into obvious distortion"
            chars.headBumpFreq = 62.0f;      // Higher head bump frequency
            chars.headBumpGain = 4.5f;       // More pronounced (the "Ampex thump")
            chars.headBumpQ = 1.4f;          // Resonant peak

            chars.hfRolloffFreq = 18000.0f;  // Slightly rolled off HF
            chars.hfRolloffSlope = -18.0f;   // Steeper rolloff (warmer)

            chars.saturationKnee = 0.75f;    // Softer knee than Studer (more gradual)
            // Ampex harmonics - slightly more color than Studer but still subtle
            // Real Ampex: THD ~0.5% at 0VU, up to ~2% when driven
            // At +6VU: 2nd ~1.5%, 3rd ~0.3%, 4th ~0.15%
            chars.saturationHarmonics[0] = 0.015f;  // 2nd harmonic (warmth)
            chars.saturationHarmonics[1] = 0.003f;  // 3rd harmonic (presence)
            chars.saturationHarmonics[2] = 0.0015f; // 4th harmonic
            chars.saturationHarmonics[3] = 0.0005f; // 5th harmonic
            chars.saturationHarmonics[4] = 0.0003f; // 6th harmonic

            chars.compressionRatio = 0.08f;  // Slightly more compression than Studer
            chars.compressionAttack = 0.15f; // Slightly slower attack
            chars.compressionRelease = 80.0f; // Longer release (musical pumping)

            chars.phaseShift = 0.04f;        // More phase shift (analog character)
            chars.crosstalkAmount = -55.0f;  // Vintage crosstalk (adds width)
            break;

        case Blend:
        default:
            // Hybrid: Best of both worlds
            chars.headBumpFreq = 55.0f;
            chars.headBumpGain = 3.75f;
            chars.headBumpQ = 1.2f;

            chars.hfRolloffFreq = 20000.0f;
            chars.hfRolloffSlope = -15.0f;

            chars.saturationKnee = 0.80f;
            // Balanced harmonic profile between Studer and Ampex
            chars.saturationHarmonics[0] = 0.012f;  // 2nd harmonic
            chars.saturationHarmonics[1] = 0.002f;  // 3rd harmonic
            chars.saturationHarmonics[2] = 0.001f;  // 4th harmonic
            chars.saturationHarmonics[3] = 0.0003f; // 5th harmonic
            chars.saturationHarmonics[4] = 0.0002f; // 6th harmonic

            chars.compressionRatio = 0.065f;
            chars.compressionAttack = 0.12f;
            chars.compressionRelease = 60.0f;

            chars.phaseShift = 0.025f;
            chars.crosstalkAmount = -62.0f;
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
            // Ampex 456 - Industry standard, warm and punchy
            // Reference tape for +6dB operating level (355nWb/m at +6 cal)
            // REAL SPEC: THD 3% at max operating level, ~0.5% at 0VU
            chars.coercivity = 0.78f;
            chars.retentivity = 0.82f;
            chars.saturationPoint = 0.88f;

            chars.hysteresisAmount = 0.08f;   // Reduced - real tape is cleaner
            chars.hysteresisAsymmetry = 0.02f;

            chars.noiseFloor = -60.0f;  // ~60dB S/N at 15 IPS
            chars.modulationNoise = 0.025f;

            chars.lfEmphasis = 1.18f;   // The "456 thump"
            chars.hfLoss = 0.90f;       // Rolls off above 16kHz at 15 IPS
            break;

        case TypeGP9:
            // 3M/Quantegy GP9 - High output, extended headroom
            // +9dB operating level capable - very clean tape
            chars.coercivity = 0.92f;
            chars.retentivity = 0.95f;
            chars.saturationPoint = 0.96f;

            chars.hysteresisAmount = 0.05f;   // Very clean, modern tape
            chars.hysteresisAsymmetry = 0.01f;

            chars.noiseFloor = -64.0f;  // Quieter than 456
            chars.modulationNoise = 0.015f;

            chars.lfEmphasis = 1.08f;   // Flatter, more modern
            chars.hfLoss = 0.96f;       // Extended HF response
            break;

        case Type911:
            // BASF/Emtec 911 - European warmth
            // Preferred for classical and acoustic recordings
            chars.coercivity = 0.82f;
            chars.retentivity = 0.86f;
            chars.saturationPoint = 0.85f;

            chars.hysteresisAmount = 0.10f;   // Slightly more character
            chars.hysteresisAsymmetry = 0.03f;

            chars.noiseFloor = -58.0f;  // Slightly higher noise
            chars.modulationNoise = 0.028f;

            chars.lfEmphasis = 1.22f;   // Warm, full low end
            chars.hfLoss = 0.88f;       // Smooth top end
            break;

        case Type250:
            // Scotch/3M 250 - Classic 1970s sound
            // Vintage character, saturates earlier than modern tape
            chars.coercivity = 0.70f;
            chars.retentivity = 0.75f;
            chars.saturationPoint = 0.80f;

            chars.hysteresisAmount = 0.15f;   // More vintage character
            chars.hysteresisAsymmetry = 0.04f;

            chars.noiseFloor = -55.0f;  // Vintage noise level
            chars.modulationNoise = 0.035f;

            chars.lfEmphasis = 1.28f;   // Big, warm low end
            chars.hfLoss = 0.85f;       // Soft, rolled HF
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

    // ========================================================================
    // NEW: Input transformer saturation (authentic tape machine signal path)
    // Real machines have transformers that add subtle coloration
    // ========================================================================
    float transformerDrive = saturationDepth * 0.5f;  // Subtle transformer effect
    signal = inputTransformer.process(signal, transformerDrive, false);

    // 1. Pre-emphasis (recording EQ)
    signal = preEmphasisFilter1.processSample(signal);
    signal = preEmphasisFilter2.processSample(signal);

    // ========================================================================
    // NEW: AC Bias oscillator effects
    // Models the linearization and HF enhancement from bias current
    // ========================================================================
    signal = biasOsc.process(signal, 100000.0f, biasAmount);

    // 2. Bias-induced HF boost (filter)
    if (biasAmount > 0.0f)
    {
        signal = biasFilter.processSample(signal);
    }

    // Calculate input level for level-dependent processing
    // 0 VU ≈ -12dBFS = 0.25 linear
    // Tape should be clean below 0 VU and saturate progressively above it
    float absInput = std::abs(signal);
    const float zeroVU = 0.25f;  // -12dBFS reference level

    // Level-dependent saturation amount: 0 below threshold, increases above
    // This makes tape nearly transparent at low levels
    float levelAboveThreshold = std::max(0.0f, (absInput - zeroVU) / (1.0f - zeroVU));
    float levelDependentSat = levelAboveThreshold * saturationDepth;

    // 3. Tape hysteresis (magnetic non-linearity) - level dependent
    // Hysteresis is minimal at low levels, increases when tape is driven hard
    // Real tape: hysteresis effect is subtle, contributes <1% THD at normal levels
    float hysteresisDepth = tapeChars.hysteresisAmount * levelDependentSat * levelDependentSat;
    signal = hysteresisProc.process(signal,
                                    hysteresisDepth,
                                    tapeChars.hysteresisAsymmetry,
                                    tapeChars.saturationPoint);

    // 4. Harmonic generation (tape saturation) - level dependent
    // Scaled to match REAL tape THD: ~0.3-0.5% at 0VU, ~1-2% at +6VU
    // Real tape is MUCH cleaner than most plugins suggest
    if (levelDependentSat > 0.01f)
    {
        float harmonics = generateHarmonics(signal, machineChars.saturationHarmonics, 5);
        // Mix in harmonics proportionally to how hard we're driving the tape
        // Real tape: THD scales with input level squared (magnetic saturation curve)
        float harmonicMix = levelDependentSat * levelDependentSat * 0.1f;  // Quadratic scaling, much gentler
        signal = signal * (1.0f - harmonicMix * 0.5f) + harmonics * harmonicMix;
    }

    // 5. Soft saturation/compression
    // Calibration affects saturation threshold (higher cal = higher threshold)
    float adjustedKnee = machineChars.saturationKnee * calibrationGain;
    float makeupGain = calibrationGain;  // Compensate for input level reduction
    signal = saturator.process(signal,
                               adjustedKnee,
                               machineChars.compressionRatio * levelDependentSat,
                               makeupGain);

    // 6. Head gap loss simulation (original filter)
    signal = gapLossFilter.processSample(signal);

    // ========================================================================
    // NEW: Playback head response
    // Models the repro head's frequency characteristics and gap effects
    // ========================================================================
    float gapWidth = (machine == Swiss800) ? 2.5f : 3.5f;  // Studer vs Ampex gap
    signal = playbackHead.process(signal, gapWidth, static_cast<float>(speed));

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

    // ========================================================================
    // 10. Wow & Flutter with NEW motor flutter component
    // Combines tape wow/flutter with capstan/motor flutter
    // ========================================================================
    if (wowFlutterAmount > 0.0f)
    {
        // NEW: Add motor flutter (machine-dependent)
        float motorQuality = (machine == Swiss800) ? 0.2f : 0.6f;  // Studer = better motor
        float motorFlutterMod = motorFlutter.calculateFlutter(motorQuality * wowFlutterAmount);

        if (sharedWowFlutterMod != nullptr)
        {
            // Use pre-calculated shared modulation for stereo coherence
            // Combine tape wow/flutter with motor flutter
            float totalModulation = *sharedWowFlutterMod + motorFlutterMod * 5.0f;  // Scale motor flutter
            signal = perChannelWowFlutter.processSample(signal, totalModulation);
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
            float totalModulation = modulation + motorFlutterMod * 5.0f;
            signal = perChannelWowFlutter.processSample(signal, totalModulation);
        }
    }

    // 11. De-emphasis (playback EQ)
    signal = deEmphasisFilter1.processSample(signal);
    signal = deEmphasisFilter2.processSample(signal);

    // ========================================================================
    // NEW: Output transformer saturation
    // Real machines have output transformers that add warmth and character
    // ========================================================================
    signal = outputTransformer.process(signal, transformerDrive * 0.7f, true);

    // 12. Add tape noise (only when noise button is enabled)
    // ABSOLUTELY NO NOISE when button is off
    if (noiseEnabled)  // Only if explicitly enabled
    {
        if (noiseAmount > 0.001f)  // And amount is meaningful
        {
            // Calculate noise level: noiseAmount is 0-1 range (parameter already divided by 100)
            // Tape noise floor is -62dB to -68dB depending on tape type
            // Speed reduction: 7.5 IPS = more noise, 30 IPS = less noise
            float noiseLevel = juce::Decibels::decibelsToGain(tapeChars.noiseFloor) *
                              speedChars.noiseReduction *
                              noiseAmount;  // noiseAmount already 0-1 from parameter scaling

            float noise = noiseGen.generateNoise(
                noiseLevel,
                tapeChars.modulationNoise,
                signal);

            // Add noise at full strength - it's already scaled appropriately
            signal += noise;
        }
    }
    // NO ELSE - when disabled, absolutely no noise is added

    // 13. DC blocking - removes subsonic rumble below 20Hz
    signal = dcBlocker.processSample(signal);

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
    // Use bandlimited soft saturation instead of tanh to prevent aliasing
    // Rational approximation: x / (1 + |x|) approaches ±1 at extremes
    // This generates fewer harmonics than tanh
    float normalizedH = H / (a + 1e-6f);
    float clampedH = juce::jlimit(-4.0f, 4.0f, normalizedH);  // Limit range
    float M_an = Ms * clampedH / (1.0f + std::abs(clampedH));

    // Differential susceptibility (rate of magnetization change)
    // Derivative of x/(1+|x|) is 1/(1+|x|)^2
    float denom = 1.0f + std::abs(clampedH);
    float dM_an = Ms / (a + 1e-6f) / (denom * denom);

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
    // Use cubic soft clip instead of tanh to reduce harmonic generation
    // This creates primarily 3rd harmonic with minimal higher harmonics
    float normalized = excess / (1.0f - threshold + 0.001f);
    float clipped = threshold + (1.0f - threshold) * (normalized - normalized * normalized * normalized / 3.0f);
    clipped = std::min(clipped, 1.0f);  // Hard limit at 1.0

    return clipped * sign;
}

// Harmonic generator using Chebyshev polynomials
// This method generates ONLY the specific harmonics requested without extra aliasing content
float ImprovedTapeEmulation::generateHarmonics(float input, const float* harmonicProfile,
                                              int numHarmonics)
{
    // Chebyshev polynomials for bandlimited harmonic generation
    // Using simple clipping instead of tanh to avoid generating infinite harmonics
    // Clamp input to [-1, 1] range for Chebyshev polynomials
    float x = juce::jlimit(-1.0f, 1.0f, input);
    float x2 = x * x;
    float x3 = x2 * x;
    float x4 = x2 * x2;
    float x5 = x3 * x2;
    float x6 = x3 * x3;

    float output = input;  // Start with fundamental

    // Scale factors reduced to match real tape THD levels
    if (numHarmonics > 0 && harmonicProfile[0] > 0.0f) {
        // 2nd harmonic (even - warmth)
        float h2 = (2.0f * x2 - 1.0f) * harmonicProfile[0];
        output += h2 * 0.3f;
    }

    if (numHarmonics > 1 && harmonicProfile[1] > 0.0f) {
        // 3rd harmonic (odd - edge/presence)
        float h3 = (4.0f * x3 - 3.0f * x) * harmonicProfile[1];
        output += h3 * 0.2f;
    }

    if (numHarmonics > 2 && harmonicProfile[2] > 0.0f) {
        // 4th harmonic (even - smoothness)
        float h4 = (8.0f * x4 - 8.0f * x2 + 1.0f) * harmonicProfile[2];
        output += h4 * 0.15f;
    }

    if (numHarmonics > 3 && harmonicProfile[3] > 0.0f) {
        // 5th harmonic (odd - bite)
        float h5 = (16.0f * x5 - 20.0f * x3 + 5.0f * x) * harmonicProfile[3];
        output += h5 * 0.1f;
    }

    if (numHarmonics > 4 && harmonicProfile[4] > 0.0f) {
        // 6th harmonic (even - air)
        float h6 = (32.0f * x6 - 48.0f * x4 + 18.0f * x2 - 1.0f) * harmonicProfile[4];
        output += h6 * 0.08f;
    }

    return output;
}
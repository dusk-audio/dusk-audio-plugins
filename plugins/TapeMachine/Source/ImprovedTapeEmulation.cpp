#include "ImprovedTapeEmulation.h"

//==============================================================================
// TransformerSaturation - Input/Output transformer coloration
//==============================================================================
void TransformerSaturation::prepare(double sampleRate)
{
    // DC blocking coefficient - ~10Hz cutoff
    dcBlockCoeff = 1.0f - (20.0f * juce::MathConstants<float>::pi / static_cast<float>(sampleRate));

    // Rate-compensated LF resonance coefficient (~50Hz cutoff regardless of sample rate)
    lfResonanceCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 50.0f
                                        / static_cast<float>(sampleRate));

    // Rate-compensated hysteresis decay (~220Hz equivalent bandwidth)
    float targetDecayRate = 220.5f;
    hystDecay = 1.0f - (targetDecayRate / static_cast<float>(sampleRate));
    hystDecay = std::clamp(hystDecay, 0.95f, 0.9999f);

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
    float signal = input;

    // DC blocking (transformer coupling)
    float dcBlocked = signal - dcState;
    dcState = signal * (1.0f - dcBlockCoeff) + dcState * dcBlockCoeff;
    signal = dcBlocked;

    // Transformer core asymmetry → even harmonics (H2, H4)
    // Real audio transformers have asymmetric B-H curves from residual
    // core magnetization, generating even-order harmonics at all signal levels.
    // For ATR-102: H2 target -52 to -58dB at 0VU.
    // y = x * (1 + b*x) where b*x² generates H2.
    // Calibrated empirically against ATR-102 H2 measurements.
    float asymmetryCoeff = 0.80f * driveAmount;
    if (asymmetryCoeff > 0.0001f)
    {
        signal = signal * (1.0f + asymmetryCoeff * signal);
    }

    // Gentle soft limiting only at extreme levels
    float absSignal = std::abs(signal);
    float saturationThreshold = isOutputStage ? 0.92f : 0.95f;

    if (absSignal > saturationThreshold)
    {
        float excess = absSignal - saturationThreshold;
        float headroom = 1.0f - saturationThreshold;
        float limited = saturationThreshold + headroom * (1.0f - std::exp(-excess * 2.0f / headroom));
        signal = (signal >= 0.0f ? 1.0f : -1.0f) * limited;
    }

    // Output transformer: subtle LF resonance from core inductance
    if (isOutputStage && driveAmount > 0.01f)
    {
        float resonanceQ = 0.15f * driveAmount;
        lfResonanceState += (signal - lfResonanceState) * lfResonanceCoeff;
        signal += lfResonanceState * resonanceQ;
    }

    // Minimal hysteresis
    float hystAmount = isOutputStage ? 0.005f : 0.002f;
    hystAmount *= driveAmount;
    float hystDelta = signal - prevInput;
    hystState = hystState * hystDecay + hystDelta * hystAmount;
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
    constexpr float targetCutoff = 740.0f;
    resonanceCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * targetCutoff
                                      / static_cast<float>(sampleRate));
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
    float speedCmPerSec = speed == 0 ? 19.05f : (speed == 1 ? 38.1f : 76.2f);
    float gapMicrons = gapWidth;

    float delayMs = (gapMicrons * 0.0001f) / speedCmPerSec * 1000.0f;
    float delaySamples = delayMs * 0.001f * static_cast<float>(currentSampleRate);
    delaySamples = std::min(delaySamples, static_cast<float>(gapDelayLine.size() - 1));

    gapDelayLine[static_cast<size_t>(gapDelayIndex)] = input;

    int readIndex = (gapDelayIndex - static_cast<int>(delaySamples) + static_cast<int>(gapDelayLine.size())) % static_cast<int>(gapDelayLine.size());
    float delayedSignal = gapDelayLine[static_cast<size_t>(readIndex)];

    gapDelayIndex = (gapDelayIndex + 1) % static_cast<int>(gapDelayLine.size());

    float gapEffect = input * 0.98f + delayedSignal * 0.02f;

    resonanceState1 += (gapEffect - resonanceState1) * resonanceCoeff;
    resonanceState2 += (resonanceState1 - resonanceState2) * resonanceCoeff;

    float resonanceBoost = (resonanceState1 - resonanceState2) * 0.15f;
    return gapEffect + resonanceBoost;
}

//==============================================================================
// MotorFlutter - Capstan and transport mechanism flutter
//==============================================================================
void MotorFlutter::prepare(double sr, int osFactor)
{
    sampleRate = sr;
    oversamplingFactor = std::max(1, osFactor);
    reset();
}

void MotorFlutter::reset()
{
    phase1 = 0.0;
    phase2 = 0.0;
    phase3 = 0.0;
}

static inline float fastSin(float x)
{
    if (!std::isfinite(x))
        return 0.0f;

    constexpr float pi = 3.14159265f;
    constexpr float twoPi = 6.28318530f;

    x = std::fmod(x, twoPi);
    if (x > pi)
        x -= twoPi;
    else if (x < -pi)
        x += twoPi;

    constexpr float B = 4.0f / pi;
    constexpr float C = -4.0f / (pi * pi);
    return B * x + C * x * std::abs(x);
}

float MotorFlutter::calculateFlutter(float motorQuality)
{
    if (motorQuality < 0.001f)
        return 0.0f;

    constexpr float twoPiF = 6.28318530f;
    float inc1 = twoPiF * 50.0f / static_cast<float>(sampleRate);
    float inc2 = twoPiF * 15.0f / static_cast<float>(sampleRate);
    float inc3 = twoPiF * 3.0f / static_cast<float>(sampleRate);

    phase1 += inc1;
    phase2 += inc2;
    phase3 += inc3;

    if (phase1 > twoPiF) phase1 -= twoPiF;
    if (phase2 > twoPiF) phase2 -= twoPiF;
    if (phase3 > twoPiF) phase3 -= twoPiF;

    float osScale = static_cast<float>(oversamplingFactor);
    float baseFlutter = motorQuality * 0.0004f * osScale;

    float motorComponent = fastSin(static_cast<float>(phase1)) * baseFlutter * 0.3f;
    float bearingComponent = fastSin(static_cast<float>(phase2)) * baseFlutter * 0.5f;
    float eccentricityComponent = fastSin(static_cast<float>(phase3)) * baseFlutter * 0.2f;

    float randomComponent = jitter(rng) * baseFlutter * 0.1f / std::sqrt(osScale);

    return motorComponent + bearingComponent + eccentricityComponent + randomComponent;
}

//==============================================================================
// ImprovedNoiseGenerator
//==============================================================================
void ImprovedNoiseGenerator::prepare(double sampleRate, int tapeSpeed)
{
    // Speed-dependent spectral tilt: lower speed = more LF noise
    // One-pole coefficient for tilt filter
    float tiltFreq = (tapeSpeed == 0) ? 800.0f :    // 7.5 IPS - more LF noise
                     (tapeSpeed == 1) ? 1500.0f :    // 15 IPS - balanced
                                        3000.0f;     // 30 IPS - more HF (less LF)

    tiltCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * tiltFreq
                                / static_cast<float>(sampleRate));

    // Envelope follower coefficient (~10ms attack/release)
    envelopeCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 100.0f
                                    / static_cast<float>(sampleRate));

    // Scrape flutter bandpass (~4kHz, Q=2)
    float fc = 4000.0f;
    float Q = 2.0f;
    float w0 = 2.0f * juce::MathConstants<float>::pi * fc / static_cast<float>(sampleRate);
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * Q);
    float a0 = 1.0f + alpha;

    scrapeBP_b0 = (alpha) / a0;
    scrapeBP_b1 = 0.0f;
    scrapeBP_b2 = (-alpha) / a0;
    scrapeBP_a1 = (-2.0f * cosw0) / a0;
    scrapeBP_a2 = (1.0f - alpha) / a0;

    reset();
}

void ImprovedNoiseGenerator::reset()
{
    b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f;
    scrapeBP_z1 = scrapeBP_z2 = 0.0f;
    envelope = 0.0f;
    tiltState = 0.0f;
}

float ImprovedNoiseGenerator::generateNoise(float noiseFloor, float modulationAmount, float signal)
{
    // Generate white noise
    float white = whiteDist(rng);

    // Paul Kellett's pink noise filter (6 stages)
    // Provides accurate -3dB/octave slope from ~40Hz to Nyquist
    b0 = 0.99886f * b0 + white * 0.0555179f;
    b1 = 0.99332f * b1 + white * 0.0750759f;
    b2 = 0.96900f * b2 + white * 0.1538520f;
    b3 = 0.86650f * b3 + white * 0.3104856f;
    b4 = 0.55000f * b4 + white * 0.5329522f;
    b5 = -0.7616f * b5 - white * 0.0168980f;

    float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.11f;
    b6 = white * 0.115926f;

    // Apply speed-dependent spectral tilt
    tiltState += (pink - tiltState) * tiltCoeff;
    float tiltedNoise = pink - tiltState * 0.5f;

    // Modulation noise: signal-dependent noise floor rise
    float absSignal = std::abs(signal);
    envelope += (absSignal - envelope) * envelopeCoeff;
    float modNoise = tiltedNoise * (1.0f + envelope * modulationAmount * 8.0f);

    // Scrape flutter: bandpass noise centered ~4kHz (head-tape contact noise)
    float scrapeWhite = whiteDist(rng);
    float scrapeOut = scrapeBP_b0 * scrapeWhite + scrapeBP_z1;
    scrapeBP_z1 = scrapeBP_b1 * scrapeWhite - scrapeBP_a1 * scrapeOut + scrapeBP_z2;
    scrapeBP_z2 = scrapeBP_b2 * scrapeWhite - scrapeBP_a2 * scrapeOut;

    // Combine: main noise + scrape flutter (subtle)
    float totalNoise = modNoise * noiseFloor + scrapeOut * noiseFloor * 0.15f;

    return totalNoise;
}

//==============================================================================
// ImprovedTapeEmulation
//==============================================================================
ImprovedTapeEmulation::ImprovedTapeEmulation()
{
    reset();
}

void ImprovedTapeEmulation::prepare(double sampleRate, int samplesPerBlock, int oversamplingFactor)
{
    if (sampleRate <= 0.0) sampleRate = 44100.0;
    if (samplesPerBlock <= 0) samplesPerBlock = 512;
    if (oversamplingFactor < 1) oversamplingFactor = 1;

    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    currentOversamplingFactor = oversamplingFactor;

    baseSampleRate = sampleRate / static_cast<double>(oversamplingFactor);

    // Anti-aliasing filter: cutoff at 0.45 * base Nyquist
    double antiAliasingCutoff = baseSampleRate * 0.45;
    antiAliasingFilter.prepare(sampleRate, antiAliasingCutoff);

    // 3-band splitter for frequency-dependent saturation
    threeBandSplitter.prepare(sampleRate);

    // Jiles-Atherton hysteresis (3 instances for bass/mid/treble)
    hysteresisBass.prepare(sampleRate, oversamplingFactor);
    hysteresisMid.prepare(sampleRate, oversamplingFactor);
    hysteresisTreble.prepare(sampleRate, oversamplingFactor);

    // Tape EQ filters
    preEmphasisEQ.prepare(sampleRate);
    deEmphasisEQ.prepare(sampleRate);

    // Phase smearing
    phaseSmear.prepare(sampleRate);

    // Improved noise generator (default to 15 IPS)
    improvedNoiseGen.prepare(sampleRate, 1);

    // Soft-clip split filter
    softClipSplitFilter.prepare(sampleRate, 5000.0);

    // Per-channel wow/flutter delay line
    perChannelWowFlutter.prepare(sampleRate, oversamplingFactor);

    // Enhanced DSP components
    inputTransformer.prepare(sampleRate);
    outputTransformer.prepare(sampleRate);
    playbackHead.prepare(sampleRate);
    motorFlutter.prepare(sampleRate, oversamplingFactor);

    reset();

    // Initialize all filters with default coefficients
    auto nyquist = sampleRate * 0.5;
    auto safeMaxFreq = nyquist * 0.9;

    auto safeFreq = [safeMaxFreq](float freq) {
        return std::min(freq, static_cast<float>(safeMaxFreq));
    };

    // Head bump filter - double precision
    auto dCoeffs = juce::dsp::IIR::Coefficients<double>::makePeakFilter(
        sampleRate, 60.0, 1.5, juce::Decibels::decibelsToGain(3.0));
    if (validateCoefficients(dCoeffs))
        headBumpFilter.coefficients = dCoeffs;

    // HF loss filters - double precision
    dCoeffs = juce::dsp::IIR::Coefficients<double>::makeLowPass(
        sampleRate, static_cast<double>(safeFreq(16000.0f)), 0.707);
    if (validateCoefficients(dCoeffs))
        hfLossFilter1.coefficients = dCoeffs;

    dCoeffs = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        sampleRate, static_cast<double>(safeFreq(10000.0f)), 0.5, juce::Decibels::decibelsToGain(-2.0));
    if (validateCoefficients(dCoeffs))
        hfLossFilter2.coefficients = dCoeffs;

    // Gap loss - double precision
    dCoeffs = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        sampleRate, static_cast<double>(safeFreq(12000.0f)), 0.707, juce::Decibels::decibelsToGain(-1.5));
    if (validateCoefficients(dCoeffs))
        gapLossFilter.coefficients = dCoeffs;

    // Bias filter (HF boost from bias current)
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, safeFreq(8000.0f), 0.707f, juce::Decibels::decibelsToGain(2.0f));
    if (validateCoefficients(coeffs))
        biasFilter.coefficients = coeffs;

    // DC blocker (subsonic filter at 25Hz)
    dCoeffs = juce::dsp::IIR::Coefficients<double>::makeHighPass(
        sampleRate, 25.0, 0.707);
    if (validateCoefficients(dCoeffs))
        dcBlocker.coefficients = dCoeffs;

    // Record head gap filter - 4th-order Butterworth at 20kHz
    recordHeadCutoff = std::min(20000.0f, static_cast<float>(safeMaxFreq));
    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 1.3066f);
    if (validateCoefficients(coeffs)) recordHeadFilter1.coefficients = coeffs;
    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 0.5412f);
    if (validateCoefficients(coeffs)) recordHeadFilter2.coefficients = coeffs;

    // Default TapeEQ settings (NAB 15 IPS)
    preEmphasisEQ.setPreEmphasis(125.0f, 50.0f);   // 8dB boost above ~3kHz
    deEmphasisEQ.setDeEmphasis(50.0f, 125.0f);     // 8dB cut (complementary)

    // Default phase smearing (Studer)
    phaseSmear.setMachineCharacter(true);

    // Saturation envelope followers
    saturator.updateCoefficients(0.1f, 10.0f, sampleRate);
}

void ImprovedTapeEmulation::reset()
{
    headBumpFilter.reset();
    hfLossFilter1.reset();
    hfLossFilter2.reset();
    gapLossFilter.reset();
    biasFilter.reset();
    dcBlocker.reset();
    recordHeadFilter1.reset();
    recordHeadFilter2.reset();
    antiAliasingFilter.reset();

    threeBandSplitter.reset();
    softClipSplitFilter.reset();

    hysteresisBass.reset();
    hysteresisMid.reset();
    hysteresisTreble.reset();

    preEmphasisEQ.reset();
    deEmphasisEQ.reset();
    phaseSmear.reset();
    improvedNoiseGen.reset();

    saturator.envelope = 0.0f;

    if (!perChannelWowFlutter.delayBuffer.empty())
    {
        std::fill(perChannelWowFlutter.delayBuffer.begin(), perChannelWowFlutter.delayBuffer.end(), 0.0f);
    }
    perChannelWowFlutter.writeIndex = 0;
    perChannelWowFlutter.allpassState = 0.0f;

    inputTransformer.reset();
    outputTransformer.reset();
    playbackHead.reset();
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
            // Studer A800 MkIII: transformerless, precision, odd-harmonic dominant
            chars.headBumpFreq = 48.0f;
            chars.headBumpGain = 3.0f;
            chars.headBumpQ = 1.0f;
            chars.hfRolloffFreq = 22000.0f;
            chars.hfRolloffSlope = -12.0f;
            chars.saturationKnee = 0.92f;
            chars.saturationHarmonics[0] = 0.003f;  // H2 minimal (no transformers)
            chars.saturationHarmonics[1] = 0.030f;  // H3 dominant (tape)
            chars.saturationHarmonics[2] = 0.001f;  // H4 minimal
            chars.saturationHarmonics[3] = 0.005f;  // H5 present
            chars.saturationHarmonics[4] = 0.0005f; // H6 minimal
            chars.compressionRatio = 0.03f;
            chars.compressionAttack = 0.08f;
            chars.compressionRelease = 40.0f;
            chars.phaseShift = 0.015f;
            chars.crosstalkAmount = -70.0f;
            break;

        case Classic102:
            // Ampex ATR-102: transformer coloration, even+odd harmonics
            chars.headBumpFreq = 62.0f;
            chars.headBumpGain = 4.5f;
            chars.headBumpQ = 1.4f;
            chars.hfRolloffFreq = 18000.0f;
            chars.hfRolloffSlope = -18.0f;
            chars.saturationKnee = 0.85f;
            chars.saturationHarmonics[0] = 0.008f;  // H2 significant (transformers)
            chars.saturationHarmonics[1] = 0.032f;  // H3 dominant (tape)
            chars.saturationHarmonics[2] = 0.003f;  // H4 (transformers)
            chars.saturationHarmonics[3] = 0.004f;  // H5 (tape)
            chars.saturationHarmonics[4] = 0.002f;  // H6 (transformers)
            chars.compressionRatio = 0.05f;
            chars.compressionAttack = 0.15f;
            chars.compressionRelease = 80.0f;
            chars.phaseShift = 0.04f;
            chars.crosstalkAmount = -55.0f;
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
            chars.coercivity = 0.78f;
            chars.retentivity = 0.82f;
            chars.saturationPoint = 0.88f;
            chars.hysteresisAmount = 0.12f;
            chars.hysteresisAsymmetry = 0.02f;
            chars.noiseFloor = -60.0f;
            chars.modulationNoise = 0.025f;
            chars.lfEmphasis = 1.12f;
            chars.hfLoss = 0.92f;
            break;

        case TypeGP9:
            chars.coercivity = 0.92f;
            chars.retentivity = 0.95f;
            chars.saturationPoint = 0.96f;
            chars.hysteresisAmount = 0.06f;
            chars.hysteresisAsymmetry = 0.01f;
            chars.noiseFloor = -64.0f;
            chars.modulationNoise = 0.015f;
            chars.lfEmphasis = 1.05f;
            chars.hfLoss = 0.96f;
            break;

        case Type911:
            chars.coercivity = 0.82f;
            chars.retentivity = 0.86f;
            chars.saturationPoint = 0.85f;
            chars.hysteresisAmount = 0.14f;
            chars.hysteresisAsymmetry = 0.025f;
            chars.noiseFloor = -58.0f;
            chars.modulationNoise = 0.028f;
            chars.lfEmphasis = 1.15f;
            chars.hfLoss = 0.90f;
            break;

        case Type250:
            chars.coercivity = 0.70f;
            chars.retentivity = 0.75f;
            chars.saturationPoint = 0.80f;
            chars.hysteresisAmount = 0.18f;
            chars.hysteresisAsymmetry = 0.035f;
            chars.noiseFloor = -55.0f;
            chars.modulationNoise = 0.035f;
            chars.lfEmphasis = 1.18f;
            chars.hfLoss = 0.87f;
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
            chars.headBumpMultiplier = 1.5f;
            chars.hfExtension = 0.7f;
            chars.noiseReduction = 1.0f;
            chars.flutterRate = 3.5f;
            chars.wowRate = 0.33f;
            break;

        case Speed_15_IPS:
            chars.headBumpMultiplier = 1.0f;
            chars.hfExtension = 1.0f;
            chars.noiseReduction = 0.7f;
            chars.flutterRate = 5.0f;
            chars.wowRate = 0.5f;
            break;

        case Speed_30_IPS:
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
                                         TapeType type, float biasAmount,
                                         EQStandard eqStandard)
{
    auto machineChars = getMachineCharacteristics(machine);
    auto tapeChars = getTapeCharacteristics(type);
    auto speedChars = getSpeedCharacteristics(speed);

    // ========================================================================
    // TapeEQFilter - NAB/CCIR/AES pre-emphasis/de-emphasis
    // Uses first-order time-constant networks via bilinear transform
    // Practical depth (~8-10dB) rather than full NAB specification (~36dB)
    // to avoid extreme gain staging while preserving correct character
    // ========================================================================
    float preEQ_tauNum = 125.0f;   // Numerator time constant (μs) - zero frequency
    float preEQ_tauDen = 50.0f;    // Denominator time constant (μs) - pole frequency

    switch (eqStandard)
    {
        case NAB:
            switch (speed)
            {
                case Speed_7_5_IPS:
                    // NAB 7.5 IPS: pole at 1768Hz (τ₂=90μs)
                    preEQ_tauNum = 225.0f;   // Zero at ~707Hz, ~8dB boost
                    preEQ_tauDen = 90.0f;
                    break;
                case Speed_15_IPS:
                    // NAB 15 IPS: pole at 3183Hz (τ₂=50μs)
                    preEQ_tauNum = 125.0f;   // Zero at ~1273Hz, ~8dB boost
                    preEQ_tauDen = 50.0f;
                    break;
                case Speed_30_IPS:
                    // NAB 30 IPS: pole at 9095Hz (τ₂=17.5μs)
                    preEQ_tauNum = 44.0f;    // Zero at ~3617Hz, ~8dB boost
                    preEQ_tauDen = 17.5f;
                    break;
            }
            break;

        case CCIR:
            switch (speed)
            {
                case Speed_7_5_IPS:
                    // CCIR 7.5 IPS: pole at 2274Hz (τ₂=70μs)
                    preEQ_tauNum = 175.0f;   // Zero at ~909Hz, ~8dB boost
                    preEQ_tauDen = 70.0f;
                    break;
                case Speed_15_IPS:
                    // CCIR 15 IPS: pole at 4547Hz (τ₂=35μs)
                    preEQ_tauNum = 88.0f;    // Zero at ~1809Hz, ~8dB boost
                    preEQ_tauDen = 35.0f;
                    break;
                case Speed_30_IPS:
                    // CCIR 30 IPS: very flat
                    preEQ_tauNum = 36.0f;
                    preEQ_tauDen = 17.5f;    // ~6dB boost
                    break;
            }
            break;

        case AES:
            // AES/IEC: minimal pre-emphasis, pole at ~9.1kHz (17.5μs HF time constant)
            preEQ_tauNum = 35.0f;        // Zero at ~4547Hz, ~6dB boost
            preEQ_tauDen = 17.5f;
            break;
    }

    // Set pre-emphasis (HF boost for recording) - tau_num > tau_den
    preEmphasisEQ.setPreEmphasis(preEQ_tauNum, preEQ_tauDen);
    // Set de-emphasis (HF cut for playback) - inverse
    deEmphasisEQ.setDeEmphasis(preEQ_tauDen, preEQ_tauNum);

    // ========================================================================
    // Configure Jiles-Atherton hysteresis for current tape type and machine
    // ========================================================================
    auto jaParams = getJAParams(type);
    bool isStuder = (machine == Swiss800);

    hysteresisBass.setFormulation(jaParams);
    hysteresisBass.setMachineType(isStuder);
    hysteresisMid.setFormulation(jaParams);
    hysteresisMid.setMachineType(isStuder);
    hysteresisTreble.setFormulation(jaParams);
    hysteresisTreble.setMachineType(isStuder);

    // ========================================================================
    // Phase smearing - machine-dependent allpass break frequencies
    // ========================================================================
    phaseSmear.setMachineCharacter(isStuder);

    // ========================================================================
    // Noise generator speed setting
    // ========================================================================
    improvedNoiseGen.prepare(currentSampleRate, static_cast<int>(speed));

    // ========================================================================
    // Head bump filter
    // ========================================================================
    float headBumpFreq = machineChars.headBumpFreq;
    float headBumpGain = machineChars.headBumpGain * speedChars.headBumpMultiplier;
    float headBumpQ = machineChars.headBumpQ;

    switch (speed)
    {
        case Speed_7_5_IPS:
            headBumpFreq = machineChars.headBumpFreq * 0.65f;
            headBumpGain *= 1.4f;
            headBumpQ *= 1.3f;
            break;
        case Speed_15_IPS:
            break;
        case Speed_30_IPS:
            headBumpFreq = machineChars.headBumpFreq * 1.5f;
            headBumpGain *= 0.7f;
            headBumpQ *= 0.8f;
            break;
    }

    headBumpGain *= tapeChars.lfEmphasis * 0.8f;

    headBumpFreq = juce::jlimit(30.0f, 120.0f, headBumpFreq);
    headBumpQ = juce::jlimit(0.7f, 2.0f, headBumpQ);
    headBumpGain = juce::jlimit(1.5f, 5.0f, headBumpGain);

    headBumpFilter.coefficients = juce::dsp::IIR::Coefficients<double>::makePeakFilter(
        currentSampleRate, static_cast<double>(headBumpFreq), static_cast<double>(headBumpQ),
        static_cast<double>(juce::Decibels::decibelsToGain(headBumpGain)));

    // ========================================================================
    // HF loss filters
    // ========================================================================
    float maxFilterFreq = static_cast<float>(currentSampleRate * 0.45);
    float hfCutoff = machineChars.hfRolloffFreq * speedChars.hfExtension * tapeChars.hfLoss;
    hfCutoff = std::min(hfCutoff, maxFilterFreq);
    hfLossFilter1.coefficients = juce::dsp::IIR::Coefficients<double>::makeLowPass(
        currentSampleRate, static_cast<double>(hfCutoff), 0.707);

    float hfShelfFreq = std::min(hfCutoff * 0.6f, maxFilterFreq);
    hfLossFilter2.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        currentSampleRate, static_cast<double>(hfShelfFreq), 0.5,
        static_cast<double>(juce::Decibels::decibelsToGain(-2.0f * tapeChars.hfLoss)));

    // ========================================================================
    // Gap loss filter
    // ========================================================================
    float gapLossFreq = speed == Speed_7_5_IPS ? 8000.0f : (speed == Speed_30_IPS ? 15000.0f : 12000.0f);
    float gapLossAmount = speed == Speed_7_5_IPS ? -3.0f : (speed == Speed_30_IPS ? -0.5f : -1.5f);
    gapLossFilter.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        currentSampleRate, static_cast<double>(gapLossFreq), 0.707,
        static_cast<double>(juce::Decibels::decibelsToGain(gapLossAmount)));

    // ========================================================================
    // Bias filter (more bias = more HF boost)
    // ========================================================================
    float biasFreq = 6000.0f + (biasAmount * 4000.0f);
    float biasGain = juce::Decibels::decibelsToGain(biasAmount * 3.0f);
    biasFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, biasFreq, 0.707f, biasGain);

    // Update saturation envelope
    saturator.updateCoefficients(machineChars.compressionAttack,
                                 machineChars.compressionRelease,
                                 currentSampleRate);
}

//==============================================================================
// Main DSP processing - corrected signal chain order
//==============================================================================
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
                                          float calibrationLevel,
                                          EQStandard eqStandard,
                                          SignalPath signalPath)
{
    // ========================================================================
    // Signal Path Modes (matching real tape machine behavior):
    //   Repro: Full tape path (record → tape → playback head)
    //   Sync:  Record head used for playback (wider gap, more HF loss)
    //   Input: Electronics only (transformers + EQ, no tape)
    //   Thru:  Complete bypass
    // ========================================================================
    if (signalPath == Thru)
        return input;

    // Denormal protection at input
    if (std::abs(input) < denormalPrevention)
        return 0.0f;

    // Update input level metering
    inputLevel.store(std::abs(input));

    // ========================================================================
    // 1. Parameter change detection → updateFilters()
    // ========================================================================
    if (machine != m_lastMachine || speed != m_lastSpeed || type != m_lastType ||
        std::abs(biasAmount - m_lastBias) > 0.01f || eqStandard != m_lastEqStandard)
    {
        updateFilters(machine, speed, type, biasAmount, eqStandard);
        m_lastMachine = machine;
        m_lastSpeed = speed;
        m_lastType = type;
        m_lastBias = biasAmount;
        m_lastEqStandard = eqStandard;

        m_cachedMachineChars = getMachineCharacteristics(machine);
        m_cachedTapeChars = getTapeCharacteristics(type);
        m_cachedSpeedChars = getSpeedCharacteristics(speed);
        m_hasTransformers = (machine == Classic102);
        m_gapWidth = (machine == Swiss800) ? 2.5f : 3.5f;
    }

    const auto& tapeChars = m_cachedTapeChars;
    const auto& speedChars = m_cachedSpeedChars;

    // Determine if we're processing tape (Repro/Sync) or electronics only (Input)
    const bool processTape = (signalPath == Repro || signalPath == Sync);

    // Sync mode uses record head for playback (wider gap = 2x normal)
    const float playbackGapWidth = (signalPath == Sync) ? m_gapWidth * 2.0f : m_gapWidth;

    // ========================================================================
    // 2. Calibration gain staging
    // ========================================================================
    float calibrationGain = juce::Decibels::decibelsToGain(calibrationLevel);
    float signal = input * 0.95f / calibrationGain;

    // ========================================================================
    // 3. Input transformer (Ampex only - Studer MkIII is transformerless)
    // ========================================================================
    float transformerDrive = m_hasTransformers ? saturationDepth * 0.3f : 0.0f;
    if (m_hasTransformers)
    {
        signal = inputTransformer.process(signal, transformerDrive, false);
    }

    // ========================================================================
    // 4. Pre-emphasis (TapeEQFilter - NAB/CCIR record EQ)
    //    Boosts HF before tape saturation for noise reduction on playback
    // ========================================================================
    signal = preEmphasisEQ.processSample(signal);

    // ========================================================================
    // TAPE PROCESSING (Repro/Sync only - skipped for Input mode)
    // ========================================================================
    if (processTape)
    {
        // ====================================================================
        // 5. Bias filter (HF boost from AC bias current)
        // ====================================================================
        if (biasAmount > 0.0f)
        {
            signal = biasFilter.processSample(signal);
        }

        // ====================================================================
        // 6. Pre-saturation soft limiter
        //    Catches extreme peaks after pre-emphasis HF boost
        // ====================================================================
        signal = preSaturationLimiter.process(signal);

        // ====================================================================
        // 7. Record head gap filter (4th-order Butterworth at 20kHz)
        //    Only when oversampling - prevents HF harmonics from aliasing
        // ====================================================================
        if (currentOversamplingFactor > 1)
        {
            signal = recordHeadFilter1.processSample(signal);
            signal = recordHeadFilter2.processSample(signal);
        }

        // ====================================================================
        // 8. 3-Band Jiles-Atherton Hysteresis Saturation
        //    Physically-based magnetic tape saturation from B-H curve
        //    Produces authentic H2/H3 harmonic spectrum
        // ====================================================================
        float tapeFormScale = 2.0f * (1.0f - tapeChars.saturationPoint) + 0.6f;
        float drive = computeDrive(saturationDepth, tapeFormScale);

        if (drive > 0.001f)
        {
            // Split into 3 frequency bands
            float bass, mid, treble;
            threeBandSplitter.split(signal, bass, mid, treble);

            // Per-band drive ratios (bass saturates less, mid full, treble minimal)
            auto ratios = getBandDriveRatios(machine);

            // Bias linearization: higher bias reduces hysteresis depth
            float biasLin = biasAmount;

            // Process each band through J-A hysteresis
            float bassSat = hysteresisBass.processSample(bass, drive * ratios.bass, biasLin);
            float midSat = hysteresisMid.processSample(mid, drive * ratios.mid, biasLin);
            float trebleSat = hysteresisTreble.processSample(treble, drive * ratios.treble, biasLin);

            // Recombine bands
            signal = bassSat + midSat + trebleSat;
        }

        // ====================================================================
        // 9. Soft clip (single stage - J-A is self-limiting so only one needed)
        //    Applied to LF content only to avoid aliasing from soft clip harmonics
        // ====================================================================
        {
            float lowFreq = softClipSplitFilter.process(signal);
            float highFreq = signal - lowFreq;
            lowFreq = softClip(lowFreq, 0.95f);
            signal = lowFreq + highFreq;
        }

        // ====================================================================
        // 10. Gap loss filter
        // ====================================================================
        signal = static_cast<float>(gapLossFilter.processSample(static_cast<double>(signal)));

        // ====================================================================
        // 11. Wow & Flutter (physically correct position - at the tape)
        // ====================================================================
        if (wowFlutterAmount > 0.0f)
        {
            float motorQuality = (machine == Swiss800) ? 0.2f : 0.6f;
            float motorFlutterMod = motorFlutter.calculateFlutter(motorQuality * wowFlutterAmount);

            if (sharedWowFlutterMod != nullptr)
            {
                float totalModulation = *sharedWowFlutterMod + motorFlutterMod * 5.0f;
                signal = perChannelWowFlutter.processSample(signal, totalModulation);
            }
            else
            {
                float modulation = perChannelWowFlutter.calculateModulation(
                    wowFlutterAmount * 0.7f,
                    wowFlutterAmount * 0.3f,
                    speedChars.wowRate,
                    speedChars.flutterRate,
                    currentSampleRate);
                float totalModulation = modulation + motorFlutterMod * 5.0f;
                signal = perChannelWowFlutter.processSample(signal, totalModulation);
            }
        }

        // ====================================================================
        // 12. Head bump resonance
        // ====================================================================
        signal = static_cast<float>(headBumpFilter.processSample(static_cast<double>(signal)));

        // ====================================================================
        // 13. HF loss (self-erasure and spacing loss)
        //     Sync mode has more HF loss due to wider record head gap
        // ====================================================================
        signal = static_cast<float>(hfLossFilter1.processSample(static_cast<double>(signal)));
        signal = static_cast<float>(hfLossFilter2.processSample(static_cast<double>(signal)));

        // Extra HF rolloff for Sync mode (record head has ~2x the gap of playback head)
        if (signalPath == Sync)
        {
            signal = static_cast<float>(hfLossFilter1.processSample(static_cast<double>(signal)));
        }

        // ====================================================================
        // 14. Playback head response (uses wider gap for Sync mode)
        // ====================================================================
        signal = playbackHead.process(signal, playbackGapWidth, static_cast<float>(speed));
    }

    // ========================================================================
    // 15. De-emphasis (TapeEQFilter - playback EQ)
    //     Restores flat response, reduces HF noise
    // ========================================================================
    signal = deEmphasisEQ.processSample(signal);

    // ========================================================================
    // 16. Phase smearing (allpass filters)
    //     Models frequency-dependent phase response of tape electronics
    // ========================================================================
    signal = phaseSmear.processSample(signal);

    // ========================================================================
    // 17. Output transformer (Ampex only)
    // ========================================================================
    if (m_hasTransformers)
    {
        signal = outputTransformer.process(signal, transformerDrive * 0.5f, true);
    }

    // ========================================================================
    // 18. Noise (Repro/Sync only - Input mode has no tape noise)
    // ========================================================================
    if (processTape && noiseEnabled && noiseAmount > 0.001f)
    {
        float noiseLevel = juce::Decibels::decibelsToGain(tapeChars.noiseFloor) *
                          speedChars.noiseReduction * noiseAmount;

        float noise = improvedNoiseGen.generateNoise(
            noiseLevel, tapeChars.modulationNoise, signal);

        signal += noise;
    }

    // ========================================================================
    // 19. DC blocker (subsonic filter at 25Hz)
    // ========================================================================
    signal = static_cast<float>(dcBlocker.processSample(static_cast<double>(signal)));

    // ========================================================================
    // 20. Anti-aliasing filter (8th-order Chebyshev, only when oversampling)
    //     Removes harmonics above original Nyquist before downsampling
    // ========================================================================
    if (currentOversamplingFactor > 1)
    {
        signal = antiAliasingFilter.process(signal);
    }

    // Denormal protection at output
    if (std::abs(signal) < denormalPrevention)
        signal = 0.0f;

    // Update output level metering
    outputLevel.store(std::abs(signal));
    gainReduction.store(std::max(0.0f, std::abs(input) - std::abs(signal)));

    return signal;
}

//==============================================================================
// TapeSaturator implementation
//==============================================================================
void ImprovedTapeEmulation::TapeSaturator::updateCoefficients(float attackMs, float releaseMs,
                                                             double sampleRate)
{
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;
    attackMs = std::max(0.001f, attackMs);
    releaseMs = std::max(0.001f, releaseMs);
    attackCoeff = std::exp(-1.0f / (attackMs * 0.001f * static_cast<float>(sampleRate)));
    releaseCoeff = std::exp(-1.0f / (releaseMs * 0.001f * static_cast<float>(sampleRate)));
}

float ImprovedTapeEmulation::TapeSaturator::process(float input, float threshold,
                                                   float ratio, float makeup)
{
    float absInput = std::abs(input);
    float targetEnv = absInput;
    float rate = (targetEnv > envelope) ? attackCoeff : releaseCoeff;
    envelope = targetEnv + (envelope - targetEnv) * rate;

    float gain = 1.0f;
    if (envelope > threshold && envelope > 0.0001f)
    {
        float excess = envelope - threshold;
        float compressedExcess = excess * (1.0f - ratio);
        gain = (threshold + compressedExcess) / envelope;
    }

    return input * gain * makeup;
}

//==============================================================================
// Soft clipping function
//==============================================================================
float ImprovedTapeEmulation::softClip(float input, float threshold)
{
    float absInput = std::abs(input);
    if (absInput < threshold)
        return input;

    float sign = (input >= 0.0f) ? 1.0f : -1.0f;
    float excess = absInput - threshold;
    float headroom = 1.0f - threshold;

    float normalized = excess / (headroom + 0.001f);
    float smoothed = normalized / (1.0f + normalized);
    float clipped = threshold + headroom * smoothed;

    return clipped * sign;
}

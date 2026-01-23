#include "ImprovedTapeEmulation.h"

//==============================================================================
// TransformerSaturation - Input/Output transformer coloration
//==============================================================================
void TransformerSaturation::prepare(double sampleRate)
{
    // DC blocking coefficient - ~10Hz cutoff
    dcBlockCoeff = 1.0f - (20.0f * juce::MathConstants<float>::pi / static_cast<float>(sampleRate));

    // Rate-compensated LF resonance coefficient (~50Hz cutoff regardless of sample rate)
    // One-pole: alpha = 1 - exp(-2*pi*fc/fs)
    lfResonanceCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 50.0f
                                        / static_cast<float>(sampleRate));

    // Rate-compensated hysteresis decay (~220Hz equivalent bandwidth)
    // At 44.1kHz: 0.995 per sample → decay rate = 0.005 * 44100 = 220.5 Hz
    // Formula: decay = 1 - (targetRate / sampleRate)
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
    // Transformer characteristics - SUBTLE coloration only
    // Real transformers add character through:
    // 1. DC blocking (coupling capacitor behavior)
    // 2. Subtle LF resonance from core inductance
    // 3. Very gentle soft limiting at extreme levels
    // NOTE: The MkIII Studer is transformerless, so this is mainly for Ampex character

    float signal = input;

    // DC blocking (transformer coupling) - this is the main effect
    float dcBlocked = signal - dcState;
    dcState = signal * (1.0f - dcBlockCoeff) + dcState * dcBlockCoeff;
    signal = dcBlocked;

    // Very gentle soft limiting only at extreme levels (>0.95)
    // Real transformers don't saturate until pushed very hard
    float absSignal = std::abs(signal);
    float saturationThreshold = isOutputStage ? 0.92f : 0.95f;

    if (absSignal > saturationThreshold)
    {
        float excess = absSignal - saturationThreshold;
        // Extremely gentle limiting - just prevents hard clipping
        float headroom = 1.0f - saturationThreshold;
        float limited = saturationThreshold + headroom * (1.0f - std::exp(-excess * 2.0f / headroom));
        signal = (signal >= 0.0f ? 1.0f : -1.0f) * limited;
    }

    // NO explicit harmonic generation here - that's handled by the main tape saturation
    // Transformers add character through frequency response, not harmonics

    // Output transformer: Very subtle LF resonance from core inductance (~40-60Hz)
    // This adds "weight" to the low end without adding harmonics
    if (isOutputStage && driveAmount > 0.01f)
    {
        // Simple resonance using rate-compensated state variable - very subtle
        float resonanceQ = 0.15f * driveAmount;  // Very subtle, scaled by drive
        lfResonanceState += (signal - lfResonanceState) * lfResonanceCoeff;
        signal += lfResonanceState * resonanceQ;
    }

    // Minimal hysteresis - just enough to add slight "thickness"
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

    // Rate-compensated resonance coefficient for head resonance filter
    // Target: ~740Hz cutoff regardless of sample rate
    // One-pole: alpha = 1 - exp(-2*pi*fc/fs)
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
    // Uses rate-compensated resonanceCoeff (computed in prepare())
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
    // AC bias in real tape runs at ~100kHz (well above audio)
    // Its effects on the audio signal are:
    // 1. Linearizes the magnetic hysteresis curve (reduces distortion)
    // 2. Slight HF emphasis (handled by biasFilter, a high shelf)
    //
    // IMPORTANT: We do NOT model the actual 100kHz oscillator or any
    // nonlinear interaction with the audio signal here. Real tape bias
    // does not create audible intermodulation because:
    // - The bias frequency is ultrasonic (100kHz)
    // - Any IM products with audio frequencies would be at 100kHz ± audio
    // - These are filtered out by the playback head's frequency response
    //
    // The "linearization" effect of bias is modeled by REDUCING the
    // saturation/hysteresis depth when bias is high (done in processSample).
    // The HF boost effect is modeled by biasFilter (linear high shelf).
    //
    // This function now passes the signal through unchanged.
    // All bias effects are modeled elsewhere in the signal chain.
    juce::ignoreUnused(biasFreq, biasAmount);
    return input;
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

// Fast sine approximation using parabolic approximation
// Accurate to ~0.1% for values in [-pi, pi], good enough for modulation
static inline float fastSin(float x)
{
    // Early check for non-finite values (NaN, inf)
    if (!std::isfinite(x))
        return 0.0f;

    constexpr float pi = 3.14159265f;
    constexpr float twoPi = 6.28318530f;

    // Safe normalization using fmod instead of while loops
    // This prevents infinite loops for extremely large values
    x = std::fmod(x, twoPi);

    // Shift into [-pi, pi] range
    if (x > pi)
        x -= twoPi;
    else if (x < -pi)
        x += twoPi;

    // Parabolic approximation: 4/pi * x - 4/pi^2 * x * |x|
    constexpr float B = 4.0f / pi;
    constexpr float C = -4.0f / (pi * pi);
    return B * x + C * x * std::abs(x);
}

float MotorFlutter::calculateFlutter(float motorQuality)
{
    // Early exit if motor quality is negligible
    if (motorQuality < 0.001f)
        return 0.0f;

    // Pre-computed phase increments (calculated once at prepare())
    // Using floats instead of doubles for speed
    constexpr float twoPiF = 6.28318530f;
    float inc1 = twoPiF * 50.0f / static_cast<float>(sampleRate);   // 50Hz motor
    float inc2 = twoPiF * 15.0f / static_cast<float>(sampleRate);   // 15Hz bearing
    float inc3 = twoPiF * 3.0f / static_cast<float>(sampleRate);    // 3Hz eccentricity

    phase1 += inc1;
    phase2 += inc2;
    phase3 += inc3;

    if (phase1 > twoPiF) phase1 -= twoPiF;
    if (phase2 > twoPiF) phase2 -= twoPiF;
    if (phase3 > twoPiF) phase3 -= twoPiF;

    // Scale deterministic modulation amplitudes by oversampling factor
    // This maintains constant TIME deviation regardless of sample rate
    float osScale = static_cast<float>(oversamplingFactor);
    float baseFlutter = motorQuality * 0.0004f * osScale;

    float motorComponent = fastSin(static_cast<float>(phase1)) * baseFlutter * 0.3f;
    float bearingComponent = fastSin(static_cast<float>(phase2)) * baseFlutter * 0.5f;
    float eccentricityComponent = fastSin(static_cast<float>(phase3)) * baseFlutter * 0.2f;

    // Random jitter: scale down by sqrt(oversamplingFactor) to maintain equal noise power
    // At 4x rate, same per-sample amplitude = 4x noise power; dividing by sqrt(4)=2 compensates
    float randomComponent = jitter(rng) * baseFlutter * 0.1f / std::sqrt(osScale);

    return motorComponent + bearingComponent + eccentricityComponent + randomComponent;
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

    // Compute base sample rate from the explicit oversampling factor.
    // The caller passes the oversampled rate and the factor used, so we can
    // derive the true base rate for anti-aliasing cutoff calculation.
    baseSampleRate = sampleRate / static_cast<double>(oversamplingFactor);

    // Configure anti-aliasing filter with cutoff at 0.45 * base Nyquist
    // This ensures harmonics above original Nyquist are attenuated before downsampling
    // At 4x oversampling (176.4kHz), cutoff = 0.45 * 22050 = ~9.9kHz relative to base
    // But we're running at oversampled rate, so actual cutoff = 0.45 * 44100 = 19.8kHz
    double antiAliasingCutoff = baseSampleRate * 0.45;
    antiAliasingFilter.prepare(sampleRate, antiAliasingCutoff);

    // Prepare 3-band splitter for frequency-dependent tape saturation
    // Bands: Bass (<200Hz), Mid (200Hz-5kHz), Treble (>5kHz)
    // Each band gets different saturation drive (bass less, mid full, treble minimal)
    threeBandSplitter.prepare(sampleRate);

    // Prepare hysteresis drive modulator with oversampling factor for rate compensation
    hysteresisMod.prepare(sampleRate, oversamplingFactor);

    // Force table regeneration with new sample rate
    saturationTable.needsRegeneration = true;

    // Soft-clip split filters (separate instances to avoid shared state contamination)
    softClipSplitFilter1.prepare(sampleRate, 5000.0);
    softClipSplitFilter2.prepare(sampleRate, 5000.0);

    // Prepare per-channel wow/flutter delay line with oversampling factor
    perChannelWowFlutter.prepare(sampleRate, oversamplingFactor);

    // Prepare new DSP components
    inputTransformer.prepare(sampleRate);
    outputTransformer.prepare(sampleRate);
    playbackHead.prepare(sampleRate);
    biasOsc.prepare(sampleRate);
    motorFlutter.prepare(sampleRate, oversamplingFactor);

    reset();

    // Initialize all filters with default coefficients for 15 IPS NAB
    // All frequencies are validated to be below Nyquist/2 for stable coefficients
    auto nyquist = sampleRate * 0.5;
    auto safeMaxFreq = nyquist * 0.9;  // Keep frequencies well below Nyquist

    // Helper lambda to safely create filter coefficients with frequency validation
    auto safeFreq = [safeMaxFreq](float freq) {
        return std::min(freq, static_cast<float>(safeMaxFreq));
    };

    // Default NAB Pre-emphasis for 15 IPS (recording EQ - boosts highs)
    // 50μs time constant = 3183 Hz corner frequency
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, safeFreq(3183.0f), 0.707f, juce::Decibels::decibelsToGain(6.0f));
    if (validateCoefficients(coeffs))
        preEmphasisFilter1.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, safeFreq(10000.0f), 2.0f, juce::Decibels::decibelsToGain(1.5f));
    if (validateCoefficients(coeffs))
        preEmphasisFilter2.coefficients = coeffs;

    // Default NAB De-emphasis for 15 IPS (playback EQ - restores flat response)
    // 3180μs time constant = 50 Hz corner frequency for LF boost
    // 50μs time constant = 3183 Hz corner frequency for HF cut
    // Double precision to avoid quantization noise at low normalized frequencies
    auto dCoeffs = juce::dsp::IIR::Coefficients<double>::makeLowShelf(
        sampleRate, 50.0, 0.707, juce::Decibels::decibelsToGain(3.0f));
    if (validateCoefficients(dCoeffs))
        deEmphasisFilter1.coefficients = dCoeffs;

    dCoeffs = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        sampleRate, static_cast<double>(safeFreq(3183.0f)), 0.707, juce::Decibels::decibelsToGain(-6.0f));
    if (validateCoefficients(dCoeffs))
        deEmphasisFilter2.coefficients = dCoeffs;

    // Head bump (characteristic low-frequency resonance) - double precision
    dCoeffs = juce::dsp::IIR::Coefficients<double>::makePeakFilter(
        sampleRate, 60.0, 1.5, juce::Decibels::decibelsToGain(3.0f));
    if (validateCoefficients(dCoeffs))
        headBumpFilter.coefficients = dCoeffs;

    // HF loss filters (tape self-erasure and spacing loss) - double precision
    dCoeffs = juce::dsp::IIR::Coefficients<double>::makeLowPass(
        sampleRate, static_cast<double>(safeFreq(16000.0f)), 0.707);
    if (validateCoefficients(dCoeffs))
        hfLossFilter1.coefficients = dCoeffs;

    dCoeffs = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        sampleRate, static_cast<double>(safeFreq(10000.0f)), 0.5, juce::Decibels::decibelsToGain(-2.0f));
    if (validateCoefficients(dCoeffs))
        hfLossFilter2.coefficients = dCoeffs;

    // Gap loss (playback head gap effect) - double precision
    dCoeffs = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        sampleRate, static_cast<double>(safeFreq(12000.0f)), 0.707, juce::Decibels::decibelsToGain(-1.5f));
    if (validateCoefficients(dCoeffs))
        gapLossFilter.coefficients = dCoeffs;

    // Bias filter (HF boost from bias current)
    coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, safeFreq(8000.0f), 0.707f, juce::Decibels::decibelsToGain(2.0f));
    if (validateCoefficients(coeffs))
        biasFilter.coefficients = coeffs;

    // Noise generator pinking filter
    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, safeFreq(3000.0f), 0.7f);
    if (validateCoefficients(coeffs))
        noiseGen.pinkingFilter.coefficients = coeffs;

    // Subsonic filter - authentic to real tape machines (Studer/Ampex have 20-30Hz filters)
    // Removes mechanical rumble and subsonic artifacts while preserving head bump (35Hz+)
    // Double precision for 25Hz at high sample rates
    dCoeffs = juce::dsp::IIR::Coefficients<double>::makeHighPass(
        sampleRate, 25.0, 0.707);
    if (validateCoefficients(dCoeffs))
        dcBlocker.coefficients = dCoeffs;

    // Record head gap filter - 4th-order Butterworth at 20kHz
    // Models the natural HF loss at the record head due to head gap geometry
    // Provides 24dB/oct rolloff above 20kHz to reduce HF before saturation.
    // The post-saturation AA filter + JUCE decimation filter handle remaining aliasing.
    //
    // 4th-order Butterworth Q values (2 biquad sections):
    // Q_k = 1/(2*sin((2k-1)*pi/(2*4))) for k=1,2
    // Q1 = 1.3066, Q2 = 0.5412
    recordHeadCutoff = 20000.0f;
    // Ensure cutoff is well below Nyquist
    recordHeadCutoff = std::min(recordHeadCutoff, static_cast<float>(safeMaxFreq));

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 1.3066f);
    if (validateCoefficients(coeffs)) recordHeadFilter1.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 0.5412f);
    if (validateCoefficients(coeffs)) recordHeadFilter2.coefficients = coeffs;

    // NOTE: Anti-aliasing filter (Chebyshev) is already initialized at the start of prepare()
    // with cutoff at 0.45 * base sample rate for proper harmonic rejection

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

    hysteresisMod.reset();
    saturationTable.needsRegeneration = true;
    threeBandSplitter.reset();

    saturator.envelope = 0.0f;

    dcBlocker.reset();
    recordHeadFilter1.reset();
    recordHeadFilter2.reset();
    antiAliasingFilter.reset();
    softClipSplitFilter1.reset();
    softClipSplitFilter2.reset();

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

    // Reset table regeneration tracking to defined state;
    // needsRegeneration=true (set above) ensures the table is always
    // regenerated on the first processSample() call after reset.
    m_lastTableMachine = Swiss800;
    m_lastTableBias = 0.0f;

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
            // The MkIII is TRANSFORMERLESS - very clean signal path
            chars.headBumpFreq = 48.0f;      // Studer head bump is lower
            chars.headBumpGain = 3.0f;       // Moderate but tight
            chars.headBumpQ = 1.0f;          // Controlled Q

            chars.hfRolloffFreq = 22000.0f;  // Extended HF (Studer is known for this)
            chars.hfRolloffSlope = -12.0f;   // Gentle rolloff

            chars.saturationKnee = 0.92f;    // Very hard knee - Studer is CLEAN until driven hard
            // Studer A800 MkIII harmonics - TRANSFORMERLESS design
            // Research: Tape saturation is primarily 3rd harmonic (odd)
            // Transformers add 2nd harmonic - but MkIII has NO transformers
            // Real Studer: THD ~0.3% at 0VU, 3% at +6VU (max operating level)
            //
            // COEFFICIENT RATIOS for polynomial saturation y = x + h2*x² + h3*x³:
            // H2 amplitude ∝ h2 * A²/2, H3 amplitude ∝ h3 * A³/4
            // To ensure H3 > H2 at all input levels (A=0.3 to 0.7):
            //   h3/h2 > 2/A, so h3/h2 > 6.7 at A=0.3
            // Using ratio of 10:1 to ensure H3 dominance even at low levels
            chars.saturationHarmonics[0] = 0.003f;  // 2nd harmonic - minimal (no transformers)
            chars.saturationHarmonics[1] = 0.030f;  // 3rd harmonic - DOMINANT (tape saturation)
            chars.saturationHarmonics[2] = 0.001f;  // 4th harmonic - minimal
            chars.saturationHarmonics[3] = 0.005f;  // 5th harmonic - odd harmonic present
            chars.saturationHarmonics[4] = 0.0005f; // 6th harmonic - minimal

            chars.compressionRatio = 0.03f;  // Very light compression until driven
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
            // Has input/output transformers that add subtle coloration
            chars.headBumpFreq = 62.0f;      // Higher head bump frequency
            chars.headBumpGain = 4.5f;       // More pronounced (the "Ampex thump")
            chars.headBumpQ = 1.4f;          // Resonant peak

            chars.hfRolloffFreq = 18000.0f;  // Slightly rolled off HF
            chars.hfRolloffSlope = -18.0f;   // Steeper rolloff (warmer)

            chars.saturationKnee = 0.85f;    // Softer knee than Studer (more gradual)
            // Ampex ATR-102 harmonics - HAS INPUT/OUTPUT TRANSFORMERS
            // Research: Tape = 3rd harmonic dominant, Transformers = 2nd harmonic
            // Ampex has both tape AND transformer coloration = mix of even+odd
            // Real Ampex: THD ~0.5% at 0VU, ~3% at +6VU (max operating level)
            //
            // COEFFICIENT RATIOS for polynomial saturation y = x + h2*x² + h3*x³:
            // Ampex has more H2 due to transformers, but H3 should still be slightly dominant
            // Using ratio of ~5:1 (H3:H2) - less than Studer's 10:1, showing transformer contribution
            // At typical levels (A=0.5), H3 will be ~0-3dB above H2 (warmer than Studer)
            chars.saturationHarmonics[0] = 0.008f;  // 2nd harmonic - significant (transformers!)
            chars.saturationHarmonics[1] = 0.032f;  // 3rd harmonic - dominant (tape saturation)
            chars.saturationHarmonics[2] = 0.003f;  // 4th harmonic - even, from transformers
            chars.saturationHarmonics[3] = 0.004f;  // 5th harmonic - odd, from tape
            chars.saturationHarmonics[4] = 0.002f;  // 6th harmonic - even, from transformers

            chars.compressionRatio = 0.05f;  // Slightly more compression than Studer
            chars.compressionAttack = 0.15f; // Slightly slower attack
            chars.compressionRelease = 80.0f; // Longer release (musical pumping)

            chars.phaseShift = 0.04f;        // More phase shift (analog character)
            chars.crosstalkAmount = -55.0f;  // Vintage crosstalk (adds width)
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

            chars.hysteresisAmount = 0.12f;   // Standard tape hysteresis
            chars.hysteresisAsymmetry = 0.02f;

            chars.noiseFloor = -60.0f;  // ~60dB S/N at 15 IPS
            chars.modulationNoise = 0.025f;

            chars.lfEmphasis = 1.12f;   // The "456 thump" - subtle
            chars.hfLoss = 0.92f;       // Rolls off above 16kHz at 15 IPS
            break;

        case TypeGP9:
            // 3M/Quantegy GP9 - High output, extended headroom
            // +9dB operating level capable - very clean tape
            chars.coercivity = 0.92f;
            chars.retentivity = 0.95f;
            chars.saturationPoint = 0.96f;

            chars.hysteresisAmount = 0.06f;   // Very clean, modern tape
            chars.hysteresisAsymmetry = 0.01f;

            chars.noiseFloor = -64.0f;  // Quieter than 456
            chars.modulationNoise = 0.015f;

            chars.lfEmphasis = 1.05f;   // Flatter, more modern
            chars.hfLoss = 0.96f;       // Extended HF response
            break;

        case Type911:
            // BASF/Emtec 911 - European warmth
            // Preferred for classical and acoustic recordings
            chars.coercivity = 0.82f;
            chars.retentivity = 0.86f;
            chars.saturationPoint = 0.85f;

            chars.hysteresisAmount = 0.14f;   // Slightly more character
            chars.hysteresisAsymmetry = 0.025f;

            chars.noiseFloor = -58.0f;  // Slightly higher noise
            chars.modulationNoise = 0.028f;

            chars.lfEmphasis = 1.15f;   // Warm, full low end
            chars.hfLoss = 0.90f;       // Smooth top end
            break;

        case Type250:
            // Scotch/3M 250 - Classic 1970s sound
            // Vintage character, saturates earlier than modern tape
            chars.coercivity = 0.70f;
            chars.retentivity = 0.75f;
            chars.saturationPoint = 0.80f;

            chars.hysteresisAmount = 0.18f;   // More vintage character
            chars.hysteresisAsymmetry = 0.035f;

            chars.noiseFloor = -55.0f;  // Vintage noise level
            chars.modulationNoise = 0.035f;

            chars.lfEmphasis = 1.18f;   // Big, warm low end
            chars.hfLoss = 0.87f;       // Soft, rolled HF
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
                                         TapeType type, float biasAmount,
                                         EQStandard eqStandard)
{
    auto machineChars = getMachineCharacteristics(machine);
    auto tapeChars = getTapeCharacteristics(type);
    auto speedChars = getSpeedCharacteristics(speed);

    // ========================================================================
    // EQ Standard Selection - NAB/CCIR/AES pre-emphasis/de-emphasis curves
    // Each standard has different time constants and frequency characteristics
    // ========================================================================
    //
    // NAB (American - National Association of Broadcasters):
    //   - Used primarily in US studios
    //   - Time constants: 50μs and 3180μs
    //   - More HF boost/cut, characteristic "American" sound
    //   - Associated with 60Hz mains hum
    //
    // CCIR/IEC (European - International Electrotechnical Commission):
    //   - Used primarily in European studios
    //   - Time constants: 70μs and 3180μs (IEC) or 35μs (CCIR at 15 IPS)
    //   - Gentler curves, slightly different character
    //   - Associated with 50Hz mains hum
    //
    // AES (Audio Engineering Society):
    //   - Modern standard, primarily for 30 IPS
    //   - Minimal pre-emphasis for extended high-frequency response
    //   - Clean, flat response

    float preEmphasisFreq = 3183.0f;   // Default 50μs time constant
    float preEmphasisGain = 6.0f;       // dB
    float deEmphasisFreq = 3183.0f;
    float deEmphasisGain = -6.0f;       // dB
    float lowFreqCompensation = 50.0f;  // 3180μs time constant = 50Hz

    // EQ Standard determines the base curves
    switch (eqStandard)
    {
        case NAB:
            // NAB curves (American standard)
            switch (speed)
            {
                case Speed_7_5_IPS:
                    // NAB 7.5 IPS: 90μs = 1768 Hz
                    preEmphasisFreq = 1768.0f;
                    preEmphasisGain = 9.0f;
                    deEmphasisFreq = 1768.0f;
                    deEmphasisGain = -9.0f;
                    lowFreqCompensation = 50.0f;
                    break;
                case Speed_15_IPS:
                    // NAB 15 IPS: 50μs = 3183 Hz (reference)
                    preEmphasisFreq = 3183.0f;
                    preEmphasisGain = 6.0f;
                    deEmphasisFreq = 3183.0f;
                    deEmphasisGain = -6.0f;
                    lowFreqCompensation = 50.0f;
                    break;
                case Speed_30_IPS:
                    // NAB 30 IPS: 35μs = 4547 Hz
                    preEmphasisFreq = 4547.0f;
                    preEmphasisGain = 4.5f;
                    deEmphasisFreq = 4547.0f;
                    deEmphasisGain = -4.5f;
                    lowFreqCompensation = 50.0f;
                    break;
            }
            break;

        case CCIR:
            // CCIR/IEC curves (European standard) - gentler HF boost
            switch (speed)
            {
                case Speed_7_5_IPS:
                    // CCIR 7.5 IPS: 70μs = 2274 Hz
                    preEmphasisFreq = 2274.0f;
                    preEmphasisGain = 7.5f;
                    deEmphasisFreq = 2274.0f;
                    deEmphasisGain = -7.5f;
                    lowFreqCompensation = 50.0f;
                    break;
                case Speed_15_IPS:
                    // CCIR 15 IPS: 35μs = 4547 Hz (flatter response than NAB)
                    preEmphasisFreq = 4547.0f;
                    preEmphasisGain = 4.5f;
                    deEmphasisFreq = 4547.0f;
                    deEmphasisGain = -4.5f;
                    lowFreqCompensation = 50.0f;
                    break;
                case Speed_30_IPS:
                    // CCIR 30 IPS: Very flat, minimal emphasis
                    preEmphasisFreq = 6000.0f;
                    preEmphasisGain = 3.0f;
                    deEmphasisFreq = 6000.0f;
                    deEmphasisGain = -3.0f;
                    lowFreqCompensation = 50.0f;
                    break;
            }
            break;

        case AES:
            // AES standard - minimal pre-emphasis for extended HF
            // Primarily used at 30 IPS for mastering
            preEmphasisFreq = 8000.0f;
            preEmphasisGain = 2.0f;
            deEmphasisFreq = 8000.0f;
            deEmphasisGain = -2.0f;
            lowFreqCompensation = 35.0f;  // Slightly higher LF corner
            break;
    }

    // Safe maximum frequency for filter design (well below Nyquist)
    float maxFilterFreq = static_cast<float>(currentSampleRate * 0.45);

    // Update pre-emphasis (recording EQ)
    preEmphasisFilter1.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, std::min(preEmphasisFreq, maxFilterFreq), 0.707f,
        juce::Decibels::decibelsToGain(preEmphasisGain));

    // Add subtle mid-range presence boost (UAD characteristic)
    float preEmph2Freq = std::min(preEmphasisFreq * 2.5f, maxFilterFreq);
    preEmphasisFilter2.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate, preEmph2Freq, 1.5f,
        juce::Decibels::decibelsToGain(1.2f));

    // Update de-emphasis (playback EQ) - compensates for pre-emphasis
    // Double precision for low-frequency precision at high sample rates
    deEmphasisFilter1.coefficients = juce::dsp::IIR::Coefficients<double>::makeLowShelf(
        currentSampleRate, static_cast<double>(lowFreqCompensation), 0.707,
        static_cast<double>(juce::Decibels::decibelsToGain(2.5f)));

    deEmphasisFilter2.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        currentSampleRate, static_cast<double>(deEmphasisFreq), 0.707,
        static_cast<double>(juce::Decibels::decibelsToGain(deEmphasisGain)));

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

    headBumpFilter.coefficients = juce::dsp::IIR::Coefficients<double>::makePeakFilter(
        currentSampleRate, static_cast<double>(headBumpFreq), static_cast<double>(headBumpQ),
        static_cast<double>(juce::Decibels::decibelsToGain(headBumpGain)));

    // Update HF loss based on tape speed and type
    // Clamp to safe frequency below Nyquist to prevent NaN coefficients
    float hfCutoff = machineChars.hfRolloffFreq * speedChars.hfExtension * tapeChars.hfLoss;
    hfCutoff = std::min(hfCutoff, maxFilterFreq);
    hfLossFilter1.coefficients = juce::dsp::IIR::Coefficients<double>::makeLowPass(
        currentSampleRate, static_cast<double>(hfCutoff), 0.707);

    float hfShelfFreq = std::min(hfCutoff * 0.6f, maxFilterFreq);
    hfLossFilter2.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        currentSampleRate, static_cast<double>(hfShelfFreq), 0.5,
        static_cast<double>(juce::Decibels::decibelsToGain(-2.0f * tapeChars.hfLoss)));

    // Gap loss is more pronounced at lower speeds
    float gapLossFreq = speed == Speed_7_5_IPS ? 8000.0f : (speed == Speed_30_IPS ? 15000.0f : 12000.0f);
    float gapLossAmount = speed == Speed_7_5_IPS ? -3.0f : (speed == Speed_30_IPS ? -0.5f : -1.5f);
    gapLossFilter.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
        currentSampleRate, static_cast<double>(gapLossFreq), 0.707,
        static_cast<double>(juce::Decibels::decibelsToGain(gapLossAmount)));

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
                                          float calibrationLevel,
                                          EQStandard eqStandard,
                                          SignalPath signalPath)
{
    // Signal Path: Thru = complete bypass
    if (signalPath == Thru)
        return input;

    // Denormal protection at input
    if (std::abs(input) < denormalPrevention)
        return 0.0f;


    // Update input level metering
    inputLevel.store(std::abs(input));

    // Update filters and cache characteristics when parameters change
    // Now also tracks EQ standard changes
    if (machine != m_lastMachine || speed != m_lastSpeed || type != m_lastType ||
        std::abs(biasAmount - m_lastBias) > 0.01f || eqStandard != m_lastEqStandard)
    {
        updateFilters(machine, speed, type, biasAmount, eqStandard);
        m_lastMachine = machine;
        m_lastSpeed = speed;
        m_lastType = type;
        m_lastBias = biasAmount;
        m_lastEqStandard = eqStandard;

        // Cache characteristics (expensive lookups done once, not per-sample)
        m_cachedMachineChars = getMachineCharacteristics(machine);
        m_cachedTapeChars = getTapeCharacteristics(type);
        m_cachedSpeedChars = getSpeedCharacteristics(speed);
        m_hasTransformers = (machine == Classic102);
        m_gapWidth = (machine == Swiss800) ? 2.5f : 3.5f;
    }

    // Use cached characteristics (fast local references)
    const auto& tapeChars = m_cachedTapeChars;
    const auto& speedChars = m_cachedSpeedChars;

    // Calibration level affects input gain staging and saturation threshold
    // Higher calibration = more headroom = tape saturates at higher input levels
    // UAD: 0dB (nominal), +3dB, +6dB, +9dB (maximum headroom)
    float calibrationGain = juce::Decibels::decibelsToGain(calibrationLevel);

    // Input gain staging (important for tape saturation)
    // Higher calibration reduces effective input level, increasing headroom
    float signal = input * 0.95f / calibrationGain;

    // ========================================================================
    // Input transformer coloration (Ampex only - Studer MkIII is transformerless)
    // Very subtle - just DC blocking and gentle limiting, no harmonic generation
    // ========================================================================
    float transformerDrive = m_hasTransformers ? saturationDepth * 0.3f : 0.0f;
    if (m_hasTransformers)
    {
        signal = inputTransformer.process(signal, transformerDrive, false);
    }

    // 1. Pre-emphasis (recording EQ) - boosts high frequencies before saturation
    // Harmonics generated by saturation are filtered by post-saturation harmonic filters
    signal = preEmphasisFilter1.processSample(signal);
    signal = preEmphasisFilter2.processSample(signal);
    // ========================================================================
    // AC Bias oscillator effects
    // Models the linearization and HF enhancement from bias current
    // ========================================================================
    signal = biasOsc.process(signal, 100000.0f, biasAmount);

    // 2. Bias-induced HF boost (filter)
    if (biasAmount > 0.0f)
    {
        signal = biasFilter.processSample(signal);
    }

    // ========================================================================
    // Pre-Saturation Soft Limiter - catches extreme peaks after pre-emphasis
    // Pre-emphasis adds +6-7dB HF boost, so +12dB input becomes +18-19dB at HF.
    // This limiter prevents those extreme peaks from generating harmonics
    // that would alias back into the audible spectrum on downsampling.
    //
    // Threshold at 0.7 (~-3dBFS) means signals at +6 VU or below pass
    // untouched. Only extreme inputs (+9 VU and above) get limited.
    // ========================================================================
    signal = preSaturationLimiter.process(signal);

    // ========================================================================
    // Record Head Gap Filter - prevents HF content from generating harmonics
    // Real tape: record head gap geometry creates natural lowpass ~15-20kHz
    // 4th-order Butterworth at 20kHz (2 cascaded biquads, 24dB/oct rolloff)
    // Only at 2x/4x where it prevents harmonics from aliasing on downsampling.
    // At 1x, HF modeling is handled by the hfLossFilter stage instead.
    // ========================================================================
    if (currentOversamplingFactor > 1)
    {
        signal = recordHeadFilter1.processSample(signal);
        signal = recordHeadFilter2.processSample(signal);
    }

    // ========================================================================
    // REALISTIC Level-Dependent Processing
    // ========================================================================
    // CLEAN H2/H3 HARMONIC SATURATION
    // Simple polynomial saturation: y = x + h2*x² + h3*x³
    //   x² produces 2nd harmonic (even - warmth, asymmetry)
    //   x³ produces 3rd harmonic (odd - presence, edge)
    //
    // REAL HARDWARE THD SPECS:
    //   Studer A800 at 0VU: ~0.3% THD, at +6VU: ~3% THD
    //   Ampex ATR-102 at 0VU: ~0.5% THD, at +6VU: ~3% THD
    //
    // TAPE FORMULATION affects THD:
    //   GP9 (high output): Least THD - highest headroom before saturation
    //   456 (standard): Reference THD level
    //   911 (European): Slightly more THD - saturates a bit earlier
    //   250 (vintage): Most THD - lowest headroom, earliest saturation
    //
    // BIAS controls H2/H3 ratio (like real tape):
    //   Low bias (0%): More H3 (gritty/edgy) - under-biased tape
    //   High bias (100%): More H2 (warm/smooth) - over-biased tape
    //   50% bias: H3 slightly dominant (authentic tape character)
    //
    // ANTI-ALIASING: Split saturation only applies to frequencies below 5kHz
    // to prevent HF harmonics from aliasing back into the audible band.
    // ========================================================================

    // ========================================================================
    // IMPROVED TAPE SATURATION MODEL
    // ========================================================================
    // Three-component model replacing simple polynomial:
    // 1. Lookup-table transfer curve (tanh-based, machine-specific asymmetry)
    // 2. 3-band frequency-dependent saturation (bass/mid/treble drive ratios)
    // 3. Hysteresis-modulated drive (history-dependent transient/sustain behavior)
    //
    // THD rises naturally with drive level (exponential mapping matches real tape):
    //   satDepth=0.5 (0VU): ~0.3% THD Studer, ~0.5% THD Ampex
    //   satDepth=0.75 (+6VU): ~3% THD (both machines)
    // ========================================================================

    // Regenerate saturation table if machine or bias changed
    {
        float defaultAsym = (machine == Swiss800) ? 0.02f : 0.15f;
        float effectiveAsym = defaultAsym * (0.3f + biasAmount * 1.4f);

        if (saturationTable.needsRegeneration
            || machine != m_lastTableMachine
            || std::abs(biasAmount - m_lastTableBias) > 0.01f)
        {
            saturationTable.generate(machine == Swiss800, effectiveAsym);
            m_lastTableMachine = machine;
            m_lastTableBias = biasAmount;
        }
    }

    // Compute drive from saturation depth with exponential mapping
    // Tape formulation affects drive: GP9 (cleanest) → 250 (most saturated)
    float tapeFormScale = 2.0f * (1.0f - tapeChars.saturationPoint) + 0.6f;
    float drive = computeDrive(saturationDepth, tapeFormScale);

    if (drive > 0.001f)
    {
        // 3-band frequency-dependent split
        float bass, mid, treble;
        threeBandSplitter.split(signal, bass, mid, treble);

        // Hysteresis-modulated drive (history-dependent)
        float hystMult = hysteresisMod.computeDriveMultiplier(
            signal, saturationDepth, tapeChars.coercivity, tapeChars.hysteresisAsymmetry);
        float modDrive = drive * hystMult;

        // Per-band saturation with machine-specific ratios
        auto ratios = getBandDriveRatios(machine);
        float bassSat = saturationTable.process(bass, modDrive * ratios.bass);
        float midSat = saturationTable.process(mid, modDrive * ratios.mid);
        float trebleSat = saturationTable.process(treble, modDrive * ratios.treble);

        // Recombine (perfect reconstruction from first-order splits)
        signal = bassSat + midSat + trebleSat;
    }

    // 5. Soft saturation/compression - gentle tape limiting behavior
    // Real tape compresses gently, doesn't hard clip
    // Apply to split LF content only to avoid aliasing from soft clip
    {
        float lowFreq = softClipSplitFilter1.process(signal);
        float highFreq = signal - lowFreq;
        lowFreq = softClip(lowFreq, 0.95f);
        signal = lowFreq + highFreq;
    }

    // 6. Head gap loss simulation (original filter)
    signal = static_cast<float>(gapLossFilter.processSample(static_cast<double>(signal)));

    // ========================================================================
    // NEW: Playback head response
    // Models the repro head's frequency characteristics and gap effects
    // ========================================================================
    signal = playbackHead.process(signal, m_gapWidth, static_cast<float>(speed));

    // 7. Apply tape formulation's frequency characteristics
    // LF emphasis based on tape type
    if (tapeChars.lfEmphasis != 1.0f)
    {
        signal *= (1.0f + (tapeChars.lfEmphasis - 1.0f) * 0.5f);
    }

    // 8. HF loss (self-erasure and spacing loss) affected by tape type
    signal = static_cast<float>(hfLossFilter1.processSample(static_cast<double>(signal)));
    signal = static_cast<float>(hfLossFilter2.processSample(static_cast<double>(signal)));

    // 9. Head bump resonance
    signal = static_cast<float>(headBumpFilter.processSample(static_cast<double>(signal)));

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
    signal = static_cast<float>(deEmphasisFilter1.processSample(static_cast<double>(signal)));
    signal = static_cast<float>(deEmphasisFilter2.processSample(static_cast<double>(signal)));

    // ========================================================================
    // Output transformer coloration (Ampex only - Studer MkIII is transformerless)
    // Very subtle - adds slight LF resonance and gentle limiting
    // ========================================================================
    if (m_hasTransformers)
    {
        signal = outputTransformer.process(signal, transformerDrive * 0.5f, true);
    }

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
    signal = static_cast<float>(dcBlocker.processSample(static_cast<double>(signal)));

    // 14. Soft clipping BEFORE anti-aliasing filter
    // ANTI-ALIASING: Split signal so only LF content is soft clipped
    // This prevents HF from generating harmonics that alias on downsampling
    {
        float lowFreqContent = softClipSplitFilter2.process(signal);
        float highFreqContent = signal - lowFreqContent;

        // Soft clip only the low frequency content
        lowFreqContent = softClip(lowFreqContent, 0.95f);

        // Recombine: clipped LF + clean HF
        signal = lowFreqContent + highFreqContent;
    }

    // 15. Post-saturation anti-aliasing filter - 8th-order Butterworth
    // CRITICAL: Only needed when oversampling is active (2x or 4x).
    // At 1x, there's no downsampling, so no aliasing can occur from harmonics.
    // Additionally, at 1x the filter cutoff (0.45*Nyquist) is too close to Nyquist
    // for the high-Q sections to be numerically stable.
    //
    // At 2x/4x oversampling:
    // - Cutoff at 0.45 * base sample rate (e.g., 19.8kHz for 44.1kHz base)
    // - Removes harmonics above original Nyquist before JUCE oversampler downsamples
    // - ~96dB attenuation at 2x cutoff frequency
    if (currentOversamplingFactor > 1)
    {
        signal = antiAliasingFilter.process(signal);
    }

    // NOTE: No further harmonic-generating processing after this point!
    // The filter MUST be the last processing stage before output.

    // Denormal protection at output
    if (std::abs(signal) < denormalPrevention)
        signal = 0.0f;

    // Update output level metering
    outputLevel.store(std::abs(signal));
    gainReduction.store(std::max(0.0f, std::abs(input) - std::abs(signal)));

    return signal;
}

//==============================================================================
// TapeSaturationTable - Pre-computed tanh-based transfer curve
//==============================================================================
void ImprovedTapeEmulation::TapeSaturationTable::generate(bool isStuder, float asymmetry)
{
    // driveK controls the steepness of the tanh curve
    // Studer: gentler curve (lower THD, ~0.3% at 0VU)
    // Ampex: steeper curve (higher THD, ~0.5% at 0VU)
    const float driveK = isStuder ? 1.6f : 2.0f;

    // DC offset creates genuine curve asymmetry for H2 generation.
    // Models imperfect bias (Studer) or transformer coupling (Ampex).
    // Offset scale: Studer needs tiny H2, Ampex needs moderate H2.
    //   Studer: H3 is 15-20dB above H2 (odd-harmonic dominant, transformerless)
    //   Ampex: H3 is 6-10dB above H2 (transformer coloration adds even harmonics)
    const float offsetScale = isStuder ? 0.25f : 0.13f;
    const float offset = asymmetry * offsetScale;
    const float dcCorrection = std::tanh(offset);

    for (int i = 0; i < TABLE_SIZE; ++i)
    {
        // Map index to input range [-2, +2]
        float x = (static_cast<float>(i) / static_cast<float>(TABLE_SIZE - 1)) * TABLE_RANGE - TABLE_RANGE * 0.5f;

        // Shifted tanh: operating point offset creates asymmetric transfer curve
        // Subtract DC to maintain zero-crossing, normalize by driveK for unity gain
        float curve = std::tanh(driveK * x + offset) - dcCorrection;
        table[static_cast<size_t>(i)] = curve / driveK;
    }

    currentAsymmetry = asymmetry;
    needsRegeneration = false;
}

float ImprovedTapeEmulation::TapeSaturationTable::process(float input, float drive) const
{
    if (drive < 0.001f)
        return input;  // Transparent when drive is negligible

    // Scale input by drive to push further into the saturation curve
    float scaledInput = input * drive;

    // Map to table index: scaledInput in [-2, +2] → index in [0, TABLE_SIZE-1]
    float normalized = (scaledInput + TABLE_RANGE * 0.5f) / TABLE_RANGE;
    normalized = std::clamp(normalized, 0.0f, 1.0f - 1e-6f);

    float indexFloat = normalized * static_cast<float>(TABLE_SIZE - 1);
    int index0 = static_cast<int>(indexFloat);
    int index1 = std::min(index0 + 1, TABLE_SIZE - 1);
    float frac = indexFloat - static_cast<float>(index0);

    // Linear interpolation
    float result = table[static_cast<size_t>(index0)] * (1.0f - frac)
                 + table[static_cast<size_t>(index1)] * frac;

    // Gain compensation: table stores tanh(k*x)/k which has slope 1 at origin.
    // After scaling input by drive, output slope = drive. Divide by drive for unity.
    return result / drive;
}

//==============================================================================
// HysteresisDriveModulator - History-dependent drive adjustment
//==============================================================================
void ImprovedTapeEmulation::HysteresisDriveModulator::prepare(double sampleRate, int osFactor)
{
    // Smoothing at ~100Hz prevents clicking from rapid offset changes
    smoothingCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 100.0f
                             / static_cast<float>(sampleRate));

    // Rate-compensated magnetic state decay:
    // At base 44.1kHz: 0.9999 per sample → decay rate = (1-0.9999)*44100 = 4.41 Hz
    // Formula: decay = 1 - (targetRate / sampleRate)
    // This ensures same time-domain decay regardless of sample rate.
    float fs = static_cast<float>(sampleRate);
    constexpr float targetDecayRate = 4.41f;  // Hz (matches original 0.9999 at 44.1kHz)
    magneticDecay = 1.0f - (targetDecayRate / fs);
    magneticDecay = std::clamp(magneticDecay, 0.99f, 0.99999f);

    // Rate-compensated tracking coefficient:
    // At base 44.1kHz: 0.1 per sample → tracking responds to ~56Hz content
    // At 4x (176.4kHz): 0.025 per sample → same ~56Hz tracking bandwidth
    // Simply divide by oversampling factor to maintain same time-domain behavior.
    int osf = std::max(1, osFactor);
    trackingCoeff = 0.1f / static_cast<float>(osf);

    reset();
}

void ImprovedTapeEmulation::HysteresisDriveModulator::reset()
{
    previousSample = 0.0f;
    magneticState = 0.0f;
    smoothedOffset = 0.0f;
}

float ImprovedTapeEmulation::HysteresisDriveModulator::computeDriveMultiplier(
    float currentSample, float saturationDepth, float coercivity, float asymmetry)
{
    // Only active when saturation is meaningful
    if (saturationDepth < 0.05f)
    {
        previousSample = currentSample;
        return 1.0f;
    }

    // Signal direction
    float dH = currentSample - previousSample;
    float magnitude = std::abs(currentSample);

    // Simplified Jiles-Atherton: magnetic state lags behind input
    // Uses rate-compensated tracking and decay coefficients
    float stateError = currentSample - magneticState;
    float stateUpdate = stateError * (1.0f - coercivity) * trackingCoeff;
    magneticState += stateUpdate;
    magneticState *= magneticDecay;  // Rate-compensated decay prevents DC accumulation

    // Drive offset based on signal direction:
    // Rising (dH > 0): less drive (cleaner transients, tape being freshly magnetized)
    // Falling (dH < 0): more drive (warmer sustain, tape retains magnetization)
    float rawOffset = 0.0f;
    if (std::abs(dH) > 1e-6f)
    {
        float direction = (dH > 0.0f) ? -1.0f : 1.0f;
        rawOffset = direction * magnitude * 0.05f * (1.0f + asymmetry);
    }

    // Scale by saturation depth (more effect when driving harder)
    rawOffset *= saturationDepth;

    // Clamp to ±5% drive modification
    rawOffset = std::clamp(rawOffset, -0.05f, 0.05f);

    // Smooth the offset to prevent clicks
    smoothedOffset = smoothedOffset * smoothingCoeff + rawOffset * (1.0f - smoothingCoeff);

    previousSample = currentSample;

    return 1.0f + smoothedOffset;
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

// Soft clipping function using rational approximation
// This provides smooth saturation with minimal harmonic generation
// The x/(1+|x|) function generates primarily odd harmonics that decay rapidly
float ImprovedTapeEmulation::softClip(float input, float threshold)
{
    float absInput = std::abs(input);
    if (absInput < threshold)
        return input;

    float sign = (input >= 0.0f) ? 1.0f : -1.0f;
    float excess = absInput - threshold;
    float headroom = 1.0f - threshold;

    // Use rational function x/(1+|x|) for smooth limiting
    // This approaches 1.0 asymptotically and never overshoots
    // Generates primarily 3rd harmonic with rapid decay of higher harmonics
    float normalized = excess / (headroom + 0.001f);
    float smoothed = normalized / (1.0f + normalized);  // Always in [0, 1)
    float clipped = threshold + headroom * smoothed;

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

    // Scale factors matched to real tape THD levels
    // Real Studer A800: ~0.3% THD at 0VU, ~3% at +6VU
    // Real Ampex ATR-102: ~0.5% THD at 0VU, ~3% at +6VU
    // The harmonicProfile values already encode machine differences,
    // these scale factors should be minimal to avoid exaggerated harmonics

    if (numHarmonics > 0 && harmonicProfile[0] > 0.0f) {
        // 2nd harmonic (even - warmth) - primary harmonic in real tape
        float h2 = (2.0f * x2 - 1.0f) * harmonicProfile[0];
        output += h2 * 0.15f;  // Reduced from 0.3f
    }

    if (numHarmonics > 1 && harmonicProfile[1] > 0.0f) {
        // 3rd harmonic (odd - edge) - typically 6-10dB below 2nd
        float h3 = (4.0f * x3 - 3.0f * x) * harmonicProfile[1];
        output += h3 * 0.08f;  // Reduced from 0.2f
    }

    if (numHarmonics > 2 && harmonicProfile[2] > 0.0f) {
        // 4th harmonic - typically 12-15dB below 2nd
        float h4 = (8.0f * x4 - 8.0f * x2 + 1.0f) * harmonicProfile[2];
        output += h4 * 0.04f;  // Reduced from 0.15f
    }

    if (numHarmonics > 3 && harmonicProfile[3] > 0.0f) {
        // 5th harmonic - very low in real tape (~-40dB relative to fundamental)
        float h5 = (16.0f * x5 - 20.0f * x3 + 5.0f * x) * harmonicProfile[3];
        output += h5 * 0.01f;  // Reduced from 0.03f
    }

    if (numHarmonics > 4 && harmonicProfile[4] > 0.0f) {
        // 6th harmonic - negligible in real tape (~-50dB relative to fundamental)
        float h6 = (32.0f * x6 - 48.0f * x4 + 18.0f * x2 - 1.0f) * harmonicProfile[4];
        output += h6 * 0.005f;  // Reduced from 0.02f
    }

    return output;
}
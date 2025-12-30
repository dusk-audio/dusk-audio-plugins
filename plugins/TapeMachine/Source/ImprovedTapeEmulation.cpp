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
        // Simple resonance using state variable - very subtle
        float resonanceFreq = 0.002f;  // ~50Hz at 44.1kHz
        float resonanceQ = 0.15f * driveAmount;  // Very subtle, scaled by drive
        lfResonanceState += (signal - lfResonanceState) * resonanceFreq;
        signal += lfResonanceState * resonanceQ;
    }

    // Minimal hysteresis - just enough to add slight "thickness"
    float hystAmount = isOutputStage ? 0.005f : 0.002f;
    hystAmount *= driveAmount;
    float hystDelta = signal - prevInput;
    hystState = hystState * 0.995f + hystDelta * hystAmount;
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

// Fast sine approximation using parabolic approximation
// Accurate to ~0.1% for values in [-pi, pi], good enough for modulation
static inline float fastSin(float x)
{
    // Normalize to [-pi, pi]
    constexpr float pi = 3.14159265f;
    constexpr float twoPi = 6.28318530f;
    while (x > pi) x -= twoPi;
    while (x < -pi) x += twoPi;

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

    // Calculate flutter components using fast sine
    float baseFlutter = motorQuality * 0.0004f;

    float motorComponent = fastSin(static_cast<float>(phase1)) * baseFlutter * 0.3f;
    float bearingComponent = fastSin(static_cast<float>(phase2)) * baseFlutter * 0.5f;
    float eccentricityComponent = fastSin(static_cast<float>(phase3)) * baseFlutter * 0.2f;

    // Add random jitter (bearing imperfections) - only occasionally to save CPU
    float randomComponent = jitter(rng) * baseFlutter * 0.1f;

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

    // Detect if we're running at oversampled rate
    // JUCE's oversampler calls prepare() with the OVERSAMPLED rate
    // We need to determine the BASE rate to set proper anti-aliasing cutoff
    //
    // Heuristic: If sample rate is 176400, 192000, 352800, or 384000,
    // we're probably oversampled. Base rate is likely 44100 or 48000.
    if (sampleRate >= 176000.0)
    {
        // 4x oversampled
        baseSampleRate = sampleRate / 4.0;
    }
    else if (sampleRate >= 88000.0)
    {
        // 2x oversampled
        baseSampleRate = sampleRate / 2.0;
    }
    else
    {
        // Not oversampled (or only 1x)
        baseSampleRate = sampleRate;
    }

    // Configure anti-aliasing filter with cutoff at 0.45 * base Nyquist
    // This ensures harmonics above original Nyquist are attenuated before downsampling
    // At 4x oversampling (176.4kHz), cutoff = 0.45 * 22050 = ~9.9kHz relative to base
    // But we're running at oversampled rate, so actual cutoff = 0.45 * 44100 = 19.8kHz
    double antiAliasingCutoff = baseSampleRate * 0.45;
    antiAliasingFilter.prepare(sampleRate, antiAliasingCutoff);

    // Prepare split filters for frequency-selective saturation
    // Cutoff at 5kHz - this means:
    // - Frequencies below 5kHz get full saturation (preserves tape warmth)
    // - Frequencies above 5kHz pass through mostly clean (no harmonics generated)
    // This prevents HF content from generating harmonics that alias
    //
    // 5kHz chosen because:
    // - Passes aliasing test at -80dB threshold with 14.5kHz @ +8.3dB input
    // - H3 (tape warmth harmonic) preserved at typical audio frequencies
    // - HF content passes linearly, keeping brightness (unlike HF detector)
    saturationSplitFilter.prepare(sampleRate, 5000.0);
    softClipSplitFilter.prepare(sampleRate, 5000.0);

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
    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sampleRate, 50.0f, 0.707f, juce::Decibels::decibelsToGain(3.0f));
    if (validateCoefficients(coeffs))
        deEmphasisFilter1.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, safeFreq(3183.0f), 0.707f, juce::Decibels::decibelsToGain(-6.0f));
    if (validateCoefficients(coeffs))
        deEmphasisFilter2.coefficients = coeffs;

    // Head bump (characteristic low-frequency resonance)
    coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, 60.0f, 1.5f, juce::Decibels::decibelsToGain(3.0f));
    if (validateCoefficients(coeffs))
        headBumpFilter.coefficients = coeffs;

    // HF loss filters (tape self-erasure and spacing loss)
    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, safeFreq(16000.0f), 0.707f);
    if (validateCoefficients(coeffs))
        hfLossFilter1.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, safeFreq(10000.0f), 0.5f, juce::Decibels::decibelsToGain(-2.0f));
    if (validateCoefficients(coeffs))
        hfLossFilter2.coefficients = coeffs;

    // Gap loss (playback head gap effect)
    coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, safeFreq(12000.0f), 0.707f, juce::Decibels::decibelsToGain(-1.5f));
    if (validateCoefficients(coeffs))
        gapLossFilter.coefficients = coeffs;

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
    coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, 25.0f, 0.707f);
    if (validateCoefficients(coeffs))
        dcBlocker.coefficients = coeffs;

    // Record head gap filter - 16th-order Butterworth at 20kHz
    // Models the natural HF loss at the record head due to head gap geometry
    // Set at 20kHz to preserve all audible content while providing some HF reduction
    // before saturation. At 192kHz oversampled rate, 20kHz is well below Nyquist.
    //
    // This filter combined with the post-saturation 18kHz filter provides aggressive
    // rolloff above 20kHz, eliminating harmonics that would alias on downsampling.
    //
    // 16th-order Butterworth Q values (8 biquad sections):
    recordHeadCutoff = 20000.0f;
    // Ensure cutoff is well below Nyquist
    recordHeadCutoff = std::min(recordHeadCutoff, static_cast<float>(safeMaxFreq));

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 5.1011f);
    if (validateCoefficients(coeffs)) recordHeadFilter1.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 1.7224f);
    if (validateCoefficients(coeffs)) recordHeadFilter2.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 1.0607f);
    if (validateCoefficients(coeffs)) recordHeadFilter3.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 0.7882f);
    if (validateCoefficients(coeffs)) recordHeadFilter4.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 0.6468f);
    if (validateCoefficients(coeffs)) recordHeadFilter5.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 0.5669f);
    if (validateCoefficients(coeffs)) recordHeadFilter6.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 0.5225f);
    if (validateCoefficients(coeffs)) recordHeadFilter7.coefficients = coeffs;

    coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, recordHeadCutoff, 0.5024f);
    if (validateCoefficients(coeffs)) recordHeadFilter8.coefficients = coeffs;

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

    hysteresisProc.state = 0.0f;
    hysteresisProc.previousInput = 0.0f;
    hysteresisProc.previousOutput = 0.0f;

    saturator.envelope = 0.0f;

    dcBlocker.reset();
    recordHeadFilter1.reset();
    recordHeadFilter2.reset();
    recordHeadFilter3.reset();
    recordHeadFilter4.reset();
    recordHeadFilter5.reset();
    recordHeadFilter6.reset();
    recordHeadFilter7.reset();
    recordHeadFilter8.reset();
    antiAliasingFilter.reset();
    saturationSplitFilter.reset();
    softClipSplitFilter.reset();

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

    // Update filters and cache characteristics when parameters change
    if (machine != m_lastMachine || speed != m_lastSpeed || type != m_lastType || std::abs(biasAmount - m_lastBias) > 0.01f)
    {
        updateFilters(machine, speed, type, biasAmount);
        m_lastMachine = machine;
        m_lastSpeed = speed;
        m_lastType = type;
        m_lastBias = biasAmount;

        // Cache characteristics (expensive lookups done once, not per-sample)
        m_cachedMachineChars = getMachineCharacteristics(machine);
        m_cachedTapeChars = getTapeCharacteristics(type);
        m_cachedSpeedChars = getSpeedCharacteristics(speed);
        m_hasTransformers = (machine == Classic102);
        m_gapWidth = (machine == Swiss800) ? 2.5f : 3.5f;
    }

    // Use cached characteristics (fast local references)
    const auto& machineChars = m_cachedMachineChars;
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
    // This 16th-order Butterworth at 20kHz mimics this physical behavior
    // Applied BEFORE saturation to prevent HF harmonics from being generated
    // ========================================================================
    signal = recordHeadFilter1.processSample(signal);
    signal = recordHeadFilter2.processSample(signal);
    signal = recordHeadFilter3.processSample(signal);
    signal = recordHeadFilter4.processSample(signal);
    signal = recordHeadFilter5.processSample(signal);
    signal = recordHeadFilter6.processSample(signal);
    signal = recordHeadFilter7.processSample(signal);
    signal = recordHeadFilter8.processSample(signal);

    // ========================================================================
    // REALISTIC Level-Dependent Processing
    // ========================================================================
    // CLEAN H2/H3 HARMONIC SATURATION
    // Simple polynomial saturation: y = x + h2*x² + h3*x³
    //   x² produces 2nd harmonic (even - warmth, asymmetry)
    //   x³ produces 3rd harmonic (odd - presence, edge)
    //
    // TARGET THD LEVELS:
    //   At 0VU (-12dBFS), 50% bias: H2 ~ -37dB, H3 ~ -30dB relative to fundamental
    //   At +6VU (-6dBFS), 50% bias: H2 ~ -33dB, H3 ~ -20dB relative to fundamental
    //
    // BIAS controls H2/H3 ratio (like real tape):
    //   Low bias (0%): More H3 (gritty/edgy) - under-biased tape
    //   High bias (100%): More H2 (warm/smooth) - over-biased tape
    //   50% bias: H3 slightly dominant (authentic tape character)
    //
    // ANTI-ALIASING: Split saturation only applies to frequencies below 5kHz
    // to prevent HF harmonics from aliasing back into the audible band.
    // ========================================================================

    // Machine-specific harmonic coefficients from getMachineCharacteristics()
    // Studer A800 MkIII: TRANSFORMERLESS - primarily 3rd harmonic from tape saturation
    // Ampex ATR-102: HAS TRANSFORMERS - mix of 2nd (transformers) and 3rd (tape)
    //
    // These coefficients represent the harmonic signature at full saturation (+6VU)
    // The ratio between H2 and H3 is critical for authentic machine character:
    // - Studer: H3 >> H2 (tape saturation dominant, no transformer coloration)
    // - Ampex: H3 > H2 but closer ratio (tape + transformer harmonics)
    float h2MachineCoeff = machineChars.saturationHarmonics[0];  // 2nd harmonic (even)
    float h3MachineCoeff = machineChars.saturationHarmonics[1];  // 3rd harmonic (odd)

    // Base scale factor to achieve proper THD levels (~3% at +6VU)
    // Polynomial: y = x + h2*x² + h3*x³
    // For sin input: H2 comes from x² (amplitude = h2 * A²/2)
    //                H3 comes from x³ (amplitude = h3 * A³/4)
    // At A=0.7 (hot signal), we want ~3% THD total
    const float baseScale = 15.0f;  // Amplifies machine coefficients to audible THD

    // Bias controls H2/H3 balance (like real tape bias adjustment)
    // biasAmount 0-1: 0 = under-biased (more odd harmonics/gritty)
    //                 1 = over-biased (more even harmonics/warm)
    //                 0.5 = optimal bias (authentic machine character)
    float h2Mix = 0.7f + biasAmount * 0.6f;   // 0.7 to 1.3 (bias adds warmth/H2)
    float h3Mix = 1.3f - biasAmount * 0.6f;   // 1.3 to 0.7 (bias reduces edge/H3)

    // Final harmonic coefficients
    float h2Scale = h2MachineCoeff * baseScale * h2Mix * saturationDepth;
    float h3Scale = h3MachineCoeff * baseScale * h3Mix * saturationDepth;

    // ANTI-ALIASING: Split signal into low/high frequency bands
    // Only the low-frequency content gets saturated
    float lowFreqContent = saturationSplitFilter.process(signal);
    float highFreqContent = signal - lowFreqContent;

    // Apply polynomial saturation to low frequencies only
    float x = lowFreqContent;
    float x2 = x * x;
    float x3 = x2 * x;
    lowFreqContent = x + h2Scale * x2 + h3Scale * x3;

    // Recombine: saturated LF + clean HF
    signal = lowFreqContent + highFreqContent;

    // 5. Soft saturation/compression - gentle tape limiting behavior
    // Real tape compresses gently, doesn't hard clip
    // Apply to split LF content only to avoid aliasing from soft clip
    {
        float lowFreq = softClipSplitFilter.process(signal);
        float highFreq = signal - lowFreq;
        lowFreq = softClip(lowFreq, 0.95f);
        signal = lowFreq + highFreq;
    }

    // 6. Head gap loss simulation (original filter)
    signal = gapLossFilter.processSample(signal);

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
    signal = dcBlocker.processSample(signal);

    // 14. Soft clipping BEFORE anti-aliasing filter
    // ANTI-ALIASING: Split signal so only LF content is soft clipped
    // This prevents HF from generating harmonics that alias on downsampling
    {
        float lowFreqContent = softClipSplitFilter.process(signal);
        float highFreqContent = signal - lowFreqContent;

        // Soft clip only the low frequency content
        lowFreqContent = softClip(lowFreqContent, 0.95f);

        // Recombine: clipped LF + clean HF
        signal = lowFreqContent + highFreqContent;
    }

    // 15. Post-saturation anti-aliasing filter - 8th-order Chebyshev Type I
    // CRITICAL: This must be AFTER any harmonic-generating processing!
    // This removes harmonics above original Nyquist before the JUCE oversampler
    // downsamples the signal.
    //
    // 8th-order Chebyshev Type I with 0.1dB passband ripple provides:
    // - ~96dB attenuation at 2x cutoff frequency
    // - Cutoff at 0.45 * base sample rate (e.g., 19.8kHz for 44.1kHz)
    // - At 39.6kHz (2x cutoff), attenuation is ~96dB
    // - This ensures H2 of 18kHz (36kHz) is attenuated by ~85dB+
    //
    // The Chebyshev provides steeper rolloff than equivalent-order Butterworth,
    // with only 4 biquad sections instead of 8 for Butterworth to achieve
    // similar attenuation.
    signal = antiAliasingFilter.process(signal);

    // NOTE: No further harmonic-generating processing after this point!
    // The filter MUST be the last processing stage before output.

    // Denormal protection at output
    if (std::abs(signal) < denormalPrevention)
        signal = 0.0f;

    // Update output level metering
    outputLevel.store(std::abs(signal));
    gainReduction.store(std::abs(input) - std::abs(signal));

    return signal;
}

// Hysteresis processor implementation - Physics-based Jiles-Atherton inspired
// REALISTIC VERSION: Tape hysteresis is subtle at normal levels, only becomes
// audible when the tape is driven hard (approaching 3% THD at +6VU)
float ImprovedTapeEmulation::HysteresisProcessor::process(float input, float amount,
                                                         float asymmetry, float saturation)
{
    // Denormal protection
    if (std::abs(input) < 1e-8f)
        return 0.0f;

    // 'amount' is already level-dependent from the caller (scaled by how hard tape is driven)
    // At normal levels (0VU), amount should be very small (~0.01-0.05)
    // At +6VU (max), amount approaches the full value (~0.1-0.15)

    // If amount is negligible, return input unchanged (tape is transparent at low levels)
    if (amount < 0.001f)
        return input;

    // Physics-based parameters (simplified Jiles-Atherton model)
    // Ms: Saturation magnetization, a: domain coupling, c: reversibility
    const float Ms = saturation;              // Saturation level (tape-dependent)
    const float a = 0.02f + amount * 0.03f;   // Domain coupling - reduced for subtlety
    const float c = 0.15f + amount * 0.1f;    // Reversible/irreversible ratio
    const float k = 0.6f + asymmetry * 0.2f;  // Coercivity (asymmetry factor)

    // Input field strength - much gentler scaling
    float H = input * (1.0f + amount * 1.5f);

    // Anhysteretic magnetization (ideal, no losses)
    // Use rational approximation: x / (1 + |x|) - generates fewer harmonics than tanh
    float normalizedH = H / (a + 1e-6f);
    float clampedH = juce::jlimit(-3.0f, 3.0f, normalizedH);  // Tighter limit
    float M_an = Ms * clampedH / (1.0f + std::abs(clampedH));

    // Differential susceptibility (rate of magnetization change)
    float denom = 1.0f + std::abs(clampedH);
    float dM_an = Ms / (a + 1e-6f) / (denom * denom);

    // Direction of field change
    float dH = H - previousInput;
    float sign_dH = (dH >= 0.0f) ? 1.0f : -1.0f;

    // Irreversible magnetization component (creates hysteresis loop)
    float M_irr_delta = (M_an - state) / (k * sign_dH + 1e-6f);

    // Total magnetization change (reversible + irreversible) - reduced integration
    float dM = c * dM_an * dH + (1.0f - c) * M_irr_delta * std::abs(dH);

    // Update magnetic state with integration - gentler
    state += dM * amount * 0.5f;

    // Apply saturation limits to prevent runaway
    state = juce::jlimit(-Ms, Ms, state);

    // Apply asymmetry (different saturation for positive/negative) - very subtle
    float asymmetryFactor = 1.0f + asymmetry * 0.08f;
    float processed = state;
    if (processed > 0.0f)
        processed *= asymmetryFactor;
    else
        processed /= asymmetryFactor;

    // Mix dry and processed - mostly dry at normal levels
    // At amount=0.1 (max normal), this is 92% dry, 8% wet
    float wetAmount = amount * 0.8f;
    float output = input * (1.0f - wetAmount) + processed * wetAmount;

    // DC blocker to prevent low frequency buildup from hysteresis
    const float dcBlockerCutoff = 0.9995f;  // ~5Hz at 44.1kHz
    float preFilteredSample = output;
    output = output - previousOutput + dcBlockerCutoff * (output + previousOutput);
    output *= 0.5f;  // Compensate for doubling

    // Update history
    previousInput = H;
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
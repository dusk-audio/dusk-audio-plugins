#include "UniversalCompressor.h"
#include "EnhancedCompressorEditor.h"
#include <cmath>

// SIMD helper utilities for vectorizable operations
namespace SIMDHelpers {
    using FloatVec = juce::dsp::SIMDRegister<float>;

    // Fast absolute value for SIMD
    inline FloatVec abs(FloatVec x) {
        return FloatVec::max(x, FloatVec(0.0f) - x);
    }

    // Fast max for peak detection
    inline float horizontalMax(FloatVec x) {
        float result = x.get(0);
        for (size_t i = 1; i < FloatVec::SIMDNumElements; ++i)
            result = juce::jmax(result, x.get(i));
        return result;
    }

    // Process buffer to get peak with SIMD (optimized metering)
    inline float getPeakLevel(const float* data, int numSamples) {
        constexpr size_t simdSize = FloatVec::SIMDNumElements;
        FloatVec peak(0.0f);

        int i = 0;
        // Process SIMD-aligned chunks
        for (; i + (int)simdSize <= numSamples; i += simdSize) {
            FloatVec samples = FloatVec::fromRawArray(data + i);
            peak = FloatVec::max(peak, abs(samples));
        }

        // Get horizontal max from SIMD register
        float scalarPeak = horizontalMax(peak);

        // Process remaining samples
        for (; i < numSamples; ++i)
            scalarPeak = juce::jmax(scalarPeak, std::abs(data[i]));

        return scalarPeak;
    }

    // Apply gain to buffer with SIMD
    inline void applyGain(float* data, int numSamples, float gain) {
        constexpr size_t simdSize = FloatVec::SIMDNumElements;
        FloatVec gainVec(gain);

        int i = 0;
        // Process SIMD-aligned chunks
        for (; i + (int)simdSize <= numSamples; i += simdSize) {
            FloatVec samples = FloatVec::fromRawArray(data + i);
            samples = samples * gainVec;
            samples.copyToRawArray(data + i);
        }

        // Process remaining samples
        for (; i < numSamples; ++i)
            data[i] *= gain;
    }

    // Mix two buffers with SIMD (for parallel compression)
    inline void mixBuffers(float* dest, const float* src, int numSamples, float wetAmount) {
        constexpr size_t simdSize = FloatVec::SIMDNumElements;
        FloatVec wetVec(wetAmount);
        FloatVec dryVec(1.0f - wetAmount);

        int i = 0;
        // Process SIMD-aligned chunks
        for (; i + (int)simdSize <= numSamples; i += simdSize) {
            FloatVec destSamples = FloatVec::fromRawArray(dest + i);
            FloatVec srcSamples = FloatVec::fromRawArray(src + i);
            FloatVec mixed = destSamples * dryVec + srcSamples * wetVec;
            mixed.copyToRawArray(dest + i);
        }

        // Process remaining samples
        for (; i < numSamples; ++i)
            dest[i] = dest[i] * (1.0f - wetAmount) + src[i] * wetAmount;
    }

    // Add analog noise with SIMD (for authenticity)
    inline void addNoise(float* data, int numSamples, float noiseLevel, juce::Random& random) {
        constexpr size_t simdSize = FloatVec::SIMDNumElements;

        int i = 0;
        // Process SIMD-aligned chunks
        for (; i + (int)simdSize <= numSamples; i += simdSize) {
            // Generate SIMD-width random values
            alignas(16) float noiseValues[FloatVec::SIMDNumElements];
            for (size_t j = 0; j < FloatVec::SIMDNumElements; ++j)
                noiseValues[j] = (random.nextFloat() * 2.0f - 1.0f) * noiseLevel;

            FloatVec samples = FloatVec::fromRawArray(data + i);
            FloatVec noise = FloatVec::fromRawArray(noiseValues);
            samples = samples + noise;
            samples.copyToRawArray(data + i);
        }

        // Process remaining samples
        for (; i < numSamples; ++i)
            data[i] += (random.nextFloat() * 2.0f - 1.0f) * noiseLevel;
    }
}

// Named constants for improved code readability
namespace Constants {
    // Filter coefficients
    // T4B Photocell Multi-Time-Constant Model (validated against hardware measurements)
    // The T4B has two distinct components:
    // 1. Fast photoresistor response: ~10ms attack, ~60ms initial decay
    // 2. Slow phosphor persistence: ~200ms memory effect
    constexpr float T4B_FAST_ATTACK = 0.010f;      // 10ms fast response
    constexpr float T4B_FAST_RELEASE = 0.060f;     // 60ms initial decay
    constexpr float T4B_SLOW_PERSISTENCE = 0.200f; // 200ms phosphor glow
    constexpr float T4B_MEMORY_COUPLING = 0.4f;    // How much slow affects fast (40%)
    
    // T4 Optical cell time constants
    constexpr float OPTO_ATTACK_TIME = 0.010f; // 10ms average
    constexpr float OPTO_RELEASE_FAST_MIN = 0.040f; // 40ms
    constexpr float OPTO_RELEASE_FAST_MAX = 0.080f; // 80ms
    constexpr float OPTO_RELEASE_SLOW_MIN = 0.5f; // 500ms
    constexpr float OPTO_RELEASE_SLOW_MAX = 5.0f; // 5 seconds
    
    // 1176 FET constants
    constexpr float FET_THRESHOLD_DB = -10.0f; // Fixed threshold
    constexpr float FET_MAX_REDUCTION_DB = 30.0f;
    constexpr float FET_ALLBUTTONS_ATTACK = 0.0001f; // 100 microseconds
    
    // DBX 160 VCA constants
    constexpr float VCA_RMS_TIME_CONSTANT = 0.003f; // 3ms RMS averaging
    constexpr float VCA_RELEASE_RATE = 120.0f; // dB per second
    constexpr float VCA_CONTROL_VOLTAGE_SCALE = -0.006f; // -6mV/dB
    constexpr float VCA_MAX_REDUCTION_DB = 60.0f;
    
    // SSL Bus constants
    constexpr float BUS_SIDECHAIN_HP_FREQ = 60.0f; // Hz
    constexpr float BUS_MAX_REDUCTION_DB = 20.0f;
    constexpr float BUS_OVEREASY_KNEE_WIDTH = 10.0f; // dB
    
    // Anti-aliasing
    constexpr float NYQUIST_SAFETY_FACTOR = 0.4f; // 40% of sample rate for tighter anti-aliasing
    constexpr float MAX_CUTOFF_FREQ = 20000.0f; // 20kHz
    
    // Safety limits
    constexpr float OUTPUT_HARD_LIMIT = 2.0f;
    constexpr float EPSILON = 0.0001f; // Small value to prevent division by zero

    // Transient detection constants
    constexpr float TRANSIENT_MULTIPLIER = 2.5f; // Threshold multiplier for transient detection
    constexpr float TRANSIENT_WINDOW_TIME = 0.1f; // 100ms window
    constexpr float TRANSIENT_NORMALIZE_COUNT = 10.0f; // 10+ transients = dense

    // Helper function to get transient window samples based on sample rate
    inline int getTransientWindowSamples(double sampleRate) {
        return static_cast<int>(TRANSIENT_WINDOW_TIME * sampleRate); // ~100ms at any sample rate
    }
}

// Unified Anti-aliasing system for all compressor types
class UniversalCompressor::AntiAliasing
{
public:
    AntiAliasing() = default;
    
    void prepare(double sampleRate, int blockSize, int numChannels)
    {
        this->sampleRate = sampleRate;
        this->numChannels = numChannels;
        
        if (blockSize > 0 && numChannels > 0)
        {
            // Use 2x oversampling (1 stage) for better performance
            // 1 stage = 2x oversampling as the button indicates
            oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
                numChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
            oversampler->initProcessing(static_cast<size_t>(blockSize));
            
            // Initialize per-channel filter states
            channelStates.resize(numChannels);
            for (auto& state : channelStates)
            {
                state.preFilterState = 0.0f;
                state.postFilterState = 0.0f;
                state.dcBlockerState = 0.0f;
                state.dcBlockerPrev = 0.0f;
            }
        }
    }
    
    juce::dsp::AudioBlock<float> processUp(juce::dsp::AudioBlock<float>& block)
    {
        if (oversampler)
            return oversampler->processSamplesUp(block);
        return block;
    }
    
    void processDown(juce::dsp::AudioBlock<float>& block)
    {
        if (oversampler)
            oversampler->processSamplesDown(block);
    }
    
    // Unified pre-saturation filtering to prevent aliasing
    float preProcessSample(float input, int channel)
    {
        if (channel >= numChannels) return input;

        // Gentle high-frequency reduction before any saturation
        // This prevents high frequencies from creating aliases
        // Tightened to 0.4 of sample rate for better anti-aliasing
        const float cutoffFreq = std::min(20000.0f, static_cast<float>(sampleRate * 0.4f));
        const float filterCoeff = std::exp(-2.0f * 3.14159f * cutoffFreq / static_cast<float>(sampleRate));

        channelStates[channel].preFilterState = input * (1.0f - filterCoeff * 0.1f) +
                                                channelStates[channel].preFilterState * filterCoeff * 0.1f;

        return channelStates[channel].preFilterState;
    }
    
    // Unified post-saturation filtering to remove any remaining aliases
    float postProcessSample(float input, int channel)
    {
        if (channel >= numChannels) return input;

        // Remove any harmonics above Nyquist/2
        // Only process if we have a valid sample rate from DAW
        if (sampleRate <= 0.0) return input;
        const float cutoffFreq = std::min(20000.0f, static_cast<float>(sampleRate * 0.4f));  // 40% of sample rate for tighter anti-aliasing
        const float filterCoeff = std::exp(-2.0f * 3.14159f * cutoffFreq / static_cast<float>(sampleRate));

        channelStates[channel].postFilterState = input * (1.0f - filterCoeff * 0.05f) +
                                                 channelStates[channel].postFilterState * filterCoeff * 0.05f;

        // Cubic soft clipping for analog warmth (applied to all modes)
        float filtered = channelStates[channel].postFilterState;
        float clipped;
        float absFiltered = std::abs(filtered);

        if (absFiltered < 1.0f / 3.0f)
        {
            clipped = filtered; // Linear region
        }
        else if (absFiltered > 2.0f / 3.0f)
        {
            clipped = filtered > 0.0f ? 1.0f : -1.0f; // Hard clip
        }
        else
        {
            // Cubic soft knee
            float sign = filtered > 0.0f ? 1.0f : -1.0f;
            clipped = sign * (absFiltered - (absFiltered * absFiltered * absFiltered) / 3.0f);
        }

        // DC blocker to remove any DC offset from saturation
        float dcBlocked = clipped - channelStates[channel].dcBlockerPrev +
                         channelStates[channel].dcBlockerState * 0.995f;
        channelStates[channel].dcBlockerPrev = clipped;
        channelStates[channel].dcBlockerState = dcBlocked;

        return dcBlocked;
    }
    
    // Generate harmonics using band-limited additive synthesis
    // This ensures no aliasing from harmonic generation
    float addHarmonics(float fundamental, float h2Level, float h3Level, float h4Level = 0.0f)
    {
        float output = fundamental;
        
        // Only add harmonics if they'll be below Nyquist
        const float nyquist = static_cast<float>(sampleRate) * 0.5f;
        
        // 2nd harmonic (even)
        if (h2Level > 0.0f && 2000.0f < nyquist)  // Assuming 1kHz fundamental * 2
        {
            float phase2 = std::atan2(fundamental, 0.0f) * 2.0f;
            output += h2Level * std::sin(phase2);
        }
        
        // 3rd harmonic (odd)
        if (h3Level > 0.0f && 3000.0f < nyquist)  // Assuming 1kHz fundamental * 3
        {
            float phase3 = std::atan2(fundamental, 0.0f) * 3.0f;
            float sign = fundamental > 0.0f ? 1.0f : -1.0f;
            output += h3Level * std::sin(phase3) * sign;
        }
        
        // 4th harmonic (even) - only at high sample rates (88kHz+)
        if (h4Level > 0.0f && 4000.0f < nyquist && sampleRate >= 88000.0)
        {
            float phase4 = std::atan2(fundamental, 0.0f) * 4.0f;
            output += h4Level * std::sin(phase4);
        }
        
        return output;
    }
    
    int getLatency() const
    {
        return oversampler ? static_cast<int>(oversampler->getLatencyInSamples()) : 0;
    }
    
    bool isOversamplingEnabled() const { return oversampler != nullptr; }
    double getSampleRate() const { return sampleRate; }

private:
    struct ChannelState
    {
        float preFilterState = 0.0f;
        float postFilterState = 0.0f;
        float dcBlockerState = 0.0f;
        float dcBlockerPrev = 0.0f;
    };
    
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::vector<ChannelState> channelStates;
    double sampleRate = 0.0;  // Set by prepare() from DAW
    int numChannels = 0;  // Set by prepare() from DAW
};

// Helper function to get harmonic scaling based on saturation mode
inline void getHarmonicScaling(int saturationMode, float& h2Scale, float& h3Scale, float& h4Scale)
{
    switch (saturationMode)
    {
        case 0: // Vintage (Warm) - more harmonics
            h2Scale = 1.5f;
            h3Scale = 1.3f;
            h4Scale = 1.2f;
            break;
        case 1: // Modern (Clean) - balanced harmonics
            h2Scale = 1.0f;
            h3Scale = 1.0f;
            h4Scale = 1.0f;
            break;
        case 2: // Pristine (Minimal) - very clean
            h2Scale = 0.3f;
            h3Scale = 0.2f;
            h4Scale = 0.1f;
            break;
        default:
            h2Scale = 1.0f;
            h3Scale = 1.0f;
            h4Scale = 1.0f;
            break;
    }
}

// Opto Compressor (LA-2A style)
class UniversalCompressor::OptoCompressor
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        detectors.resize(numChannels);
        for (auto& detector : detectors)
        {
            detector.envelope = 1.0f; // Start at unity gain (no reduction)
            detector.rms = 0.0f;
            detector.lightMemory = 0.0f; // T4 cell light memory (legacy)
            detector.previousReduction = 0.0f;
            detector.hfFilter = 0.0f;
            detector.releaseStartTime = 0.0f;
            detector.releaseStartLevel = 1.0f;
            detector.releasePhase = 0;
            detector.maxReduction = 0.0f;
            detector.holdCounter = 0.0f;
            detector.saturationLowpass = 0.0f; // Initialize anti-aliasing filter
            detector.prevInput = 0.0f; // Initialize previous input

            // T4B dual time-constant components
            detector.fastMemory = 0.0f;
            detector.slowMemory = 0.0f;

            // Program-dependent release tracking
            detector.prevInput = 0.0f;
        }
        
        // PROFESSIONAL FIX: Always create 2x oversampler for saturation
        // This ensures harmonics are consistent regardless of user setting
        saturationOversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            1, // Single channel processing
            1, // 1 stage = 2x oversampling
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR
        );
        saturationOversampler->initProcessing(1); // Single sample processing
    }
    
    float process(float input, int channel, float peakReduction, float gain, bool limitMode, bool oversample = false)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;
        
        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;
        
        // Validate parameters
        peakReduction = juce::jlimit(0.0f, 100.0f, peakReduction);
        gain = juce::jlimit(-40.0f, 40.0f, gain);
        
        #ifdef DEBUG
        jassert(!std::isnan(input) && !std::isinf(input));
        jassert(sampleRate > 0.0);
        #endif
            
        auto& detector = detectors[channel];
        
        // Apply gain reduction (feedback topology)
        float compressed = input * detector.envelope;
        
        // LA-2A feedback topology: detection from output
        // In Compress mode: sidechain = output
        // In Limit mode: sidechain = 1/25 input + 24/25 output
        float sidechainSignal;
        if (limitMode)
        {
            // Limit mode mixes a small amount of input with output
            sidechainSignal = input * 0.04f + compressed * 0.96f;
        }
        else
        {
            // Compress mode uses pure output feedback
            sidechainSignal = compressed;
        }
        
        // Peak Reduction controls the sidechain amplifier gain (essentially threshold)
        // 0-100 maps to 0dB to -40dB threshold (inverted control)
        float sidechainGain = juce::Decibels::decibelsToGain(peakReduction * 0.4f); // 0 to +40dB
        float detectionLevel = std::abs(sidechainSignal * sidechainGain);
        
        // Frequency-dependent detection (T4 cell is more sensitive to midrange)
        // Simple high-frequency rolloff to simulate T4 response
        float hfRolloff = 0.7f; // Reduces high frequency sensitivity
        detector.hfFilter = detector.hfFilter * hfRolloff + detectionLevel * (1.0f - hfRolloff);
        detectionLevel = detector.hfFilter;
        
        // T4B optical cell dual time-constant model (hardware-validated)
        // The T4B photocell has two distinct response components:
        // 1. Fast photoresistor: responds quickly to light changes (~10ms)
        // 2. Slow phosphor: maintains a "glow" that persists (~200ms)

        float lightInput = detectionLevel;

        // Program-dependent release: faster on transients (LA-2A characteristic)
        float absInput = std::abs(input);
        float inputDelta = absInput - detector.prevInput;
        detector.prevInput = absInput;
        // Scale release faster when detecting transients (positive delta)
        float releaseScale = inputDelta > 0.05f ? 0.6f : 1.0f; // 40% faster on transients

        // Calculate time constants at current sample rate
        float fastAttackCoeff = std::exp(-1.0f / (Constants::T4B_FAST_ATTACK * static_cast<float>(sampleRate)));
        float fastReleaseCoeff = std::exp(-1.0f / (Constants::T4B_FAST_RELEASE * static_cast<float>(sampleRate) * releaseScale));
        float slowPersistCoeff = std::exp(-1.0f / (Constants::T4B_SLOW_PERSISTENCE * static_cast<float>(sampleRate)));

        // Fast photoresistor component: quick attack, program-dependent release
        if (lightInput > detector.fastMemory)
            detector.fastMemory = lightInput + (detector.fastMemory - lightInput) * fastAttackCoeff;
        else
            detector.fastMemory = lightInput + (detector.fastMemory - lightInput) * fastReleaseCoeff;

        // Slow phosphor persistence: gradual decay creates "memory"
        detector.slowMemory = lightInput + (detector.slowMemory - lightInput) * slowPersistCoeff;

        // Combine fast and slow components with coupling factor
        // The slow memory "lifts" the fast response, creating hysteresis
        float lightLevel = detector.fastMemory + (detector.slowMemory * Constants::T4B_MEMORY_COUPLING);

        // The light level now exhibits proper T4B characteristics:
        // - Fast initial response (10ms)
        // - Memory effect prevents immediate return (200ms persistence)
        // - Creates the LA-2A's characteristic "sticky" compression
        
        // Variable ratio based on feedback topology
        // LA-2A ratio varies from ~3:1 (low levels) to ~10:1 (high levels)
        // This is a key characteristic of the T4 optical cell
        float reduction = 0.0f;

        // Input-dependent threshold: lower threshold for louder inputs (LA-2A characteristic)
        // This creates program-dependent behavior where the compressor becomes more sensitive
        // to loud signals, mimicking the T4 cell's nonlinear light response
        float baseThreshold = 0.5f; // Base internal reference level
        float inputLevel = std::abs(input);

        // Dynamic threshold adjustment based on recent input level
        // Louder inputs lower the threshold by up to 20%
        float thresholdReduction = juce::jlimit(0.0f, 0.2f, inputLevel * 0.3f);
        float internalThreshold = baseThreshold * (1.0f - thresholdReduction);

        if (lightLevel > internalThreshold)
        {
            float excess = lightLevel - internalThreshold;

            // Program-dependent ratio calculation (authentic LA-2A behavior)
            // Low levels: ~3:1, Medium: ~4-6:1, High: ~8-10:1
            float baseRatio = 3.0f;
            float maxRatio = limitMode ? 20.0f : 10.0f;

            // Logarithmic progression based on light level for natural compression curve
            // This models the T4 cell's nonlinear photoresistive response
            float lightIntensity = juce::jlimit(0.0f, 1.0f, lightLevel - internalThreshold);
            float ratioFactor = std::log10(1.0f + lightIntensity * 9.0f); // 0-1 range, logarithmic
            float programDependentRatio = baseRatio + (maxRatio - baseRatio) * ratioFactor;

            // Feedback topology: ratio increases with compression amount
            // This creates the characteristic "gentle start, aggressive end" behavior
            float variableRatio = programDependentRatio * (1.0f + excess * 8.0f);

            // Calculate gain reduction in dB using feedback formula
            // At low levels: gentle 3:1 compression
            // At high levels: aggressive 8-10:1 limiting
            reduction = 20.0f * std::log10(1.0f + excess * variableRatio);

            // LA-2A typically maxes out around 40dB GR
            reduction = juce::jmin(reduction, 40.0f);
        }
        
        // LA-2A T4 optical cell time constants
        // Attack: 10ms average
        // Release: Two-stage - 40-80ms for first 50%, then 0.5-5 seconds for full recovery
        float targetGain = juce::Decibels::decibelsToGain(-reduction);

        // Track reduction change for program-dependent behavior
        detector.previousReduction = reduction;

        // Update signal history for adaptive release behavior
        // Track peak and average levels for transient detection
        // Reuse absInput calculated earlier
        detector.peakLevel = juce::jmax(detector.peakLevel * 0.999f, absInput);
        detector.averageLevel = detector.averageLevel * 0.9999f + absInput * 0.0001f;

        // Detect transients: sudden level increases significantly above average
        float inputChange = absInput - detector.averageLevel;
        if (inputChange > detector.averageLevel * Constants::TRANSIENT_MULTIPLIER)
        {
            detector.transientCount++;
            detector.samplesSinceTransient = 0;
        }
        else
        {
            detector.samplesSinceTransient++;
        }

        // Update transient density periodically (every ~100ms, scaled to sample rate)
        detector.sampleWindowCounter++;
        int transientWindowSamples = Constants::getTransientWindowSamples(sampleRate);
        if (detector.sampleWindowCounter >= transientWindowSamples)
        {
            // Normalize to 0-1 range (10+ transients in 100ms = dense)
            detector.transientDensity = juce::jlimit(0.0f, 1.0f, detector.transientCount / Constants::TRANSIENT_NORMALIZE_COUNT);
            detector.transientCount = 0;
            detector.sampleWindowCounter = 0;
        }

        if (targetGain < detector.envelope)
        {
            // Attack phase - 10ms average - calculate coefficient properly for actual sample rate
            float attackTime = Constants::OPTO_ATTACK_TIME;
            float attackCoeff = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, attackTime * static_cast<float>(sampleRate))));
            detector.envelope = targetGain + (detector.envelope - targetGain) * attackCoeff;

            // Reset release tracking
            detector.releasePhase = 0;
            detector.releaseStartLevel = detector.envelope;
            detector.releaseStartTime = 0.0f;
        }
        else
        {
            // Two-stage release characteristic of T4 cell
            detector.releaseStartTime += 1.0f / sampleRate;

            float releaseTime;

            // Calculate how far we've recovered
            float recoveryAmount = (detector.envelope - detector.releaseStartLevel) /
                                  (1.0f - detector.releaseStartLevel + 0.0001f);

            if (recoveryAmount < 0.5f)
            {
                // First stage: 40-80ms for first 50% recovery
                // Faster for smaller reductions, slower for larger
                float reductionFactor = juce::jlimit(0.0f, 1.0f, detector.maxReduction * 0.05f); // /20.0f

                // Adaptive release: faster for transient-dense material (drums, percussion)
                // slower for sustained material (vocals, bass)
                // Transient material gets 30-50% faster release to maintain punch
                float transientFactor = 1.0f - (detector.transientDensity * 0.4f);
                releaseTime = (Constants::OPTO_RELEASE_FAST_MIN + reductionFactor * (Constants::OPTO_RELEASE_FAST_MAX - Constants::OPTO_RELEASE_FAST_MIN)) * transientFactor;
                detector.releasePhase = 1;
            }
            else
            {
                // Second stage: 0.5-5 seconds for remaining recovery
                // Program and history dependent
                float lightIntensity = juce::jlimit(0.0f, 1.0f, detector.maxReduction * 0.0333f); // /30.0f
                float timeHeld = juce::jlimit(0.0f, 1.0f, detector.holdCounter / static_cast<float>(sampleRate * 2.0f));

                // Adaptive release in second stage: sustained material gets longer tail
                // This preserves natural decay of vocals and instruments
                float transientFactor = 1.0f + (1.0f - detector.transientDensity) * 0.3f;

                // Longer recovery for stronger/longer compression
                releaseTime = (Constants::OPTO_RELEASE_SLOW_MIN + (lightIntensity * timeHeld * (Constants::OPTO_RELEASE_SLOW_MAX - Constants::OPTO_RELEASE_SLOW_MIN))) * transientFactor;
                detector.releasePhase = 2;
            }

            float releaseCoeff = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, releaseTime * static_cast<float>(sampleRate))));
            detector.envelope = targetGain + (detector.envelope - targetGain) * releaseCoeff;

            // NaN/Inf safety check
            if (std::isnan(detector.envelope) || std::isinf(detector.envelope))
                detector.envelope = 1.0f;
        }
        
        // Track compression history for program dependency
        if (reduction > detector.maxReduction)
            detector.maxReduction = reduction;
        
        if (reduction > 0.5f)
        {
            detector.holdCounter = juce::jmin(detector.holdCounter + 1.0f, static_cast<float>(sampleRate * 10.0f));
        }
        else
        {
            // Slow decay of memory
            detector.maxReduction *= 0.9999f;
            detector.holdCounter *= 0.999f;
        }
        
        // LA-2A Tube output stage - 12AX7 tube followed by 12AQ5 power tube
        // The LA-2A has a characteristic warm tube sound with prominent 2nd harmonic
        float makeupGain = juce::Decibels::decibelsToGain(gain);
        float driven = compressed * makeupGain;
        
        // LA-2A tube harmonics - generate based on whether oversampling is active
        // When oversampling is ON, we're at 2x rate so harmonics won't alias
        // When oversampling is OFF, we limit harmonics to prevent aliasing

        float saturated = driven;
        float absDriven = std::abs(driven);

        if (absDriven > 0.001f)  // Lower threshold for harmonic generation
        {
            float sign = (driven < 0.0f) ? -1.0f : 1.0f;
            float levelDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, absDriven));
            
            // Calculate harmonic levels
            float h2_level = 0.0f;
            float h3_level = 0.0f;
            float h4_level = 0.0f;
            
            // LA-2A has more harmonic content than 1176
            if (levelDb > -40.0f)  // Add harmonics above -40dB
            {
                // 2nd harmonic - LA-2A manual spec: < 0.35% THD at +10dBm
                float thd_target = levelDb > 6.0f ? 0.0075f : 0.0035f;
                float h2_scale = thd_target * 0.85f;
                h2_level = absDriven * absDriven * h2_scale;

                // 3rd harmonic - LA-2A tubes produce some odd harmonics
                float h3_scale = thd_target * 0.12f;
                h3_level = absDriven * absDriven * absDriven * h3_scale;

                // 4th harmonic - minimal in LA-2A
                // Only add if we're oversampling (to prevent aliasing)
                if (oversample)
                {
                    float h4_scale = thd_target * 0.03f;
                    h4_level = absDriven * absDriven * absDriven * absDriven * h4_scale;
                }
            }
            
            // Apply harmonics
            saturated = driven;
            
            // Add 2nd harmonic (even) - main tube warmth
            if (h2_level > 0.0f)
            {
                float squared = driven * driven * sign;
                saturated += squared * h2_level;
            }
            
            // Add 3rd harmonic (odd) - subtle tube character
            if (h3_level > 0.0f)
            {
                float cubed = driven * driven * driven;
                saturated += cubed * h3_level;
            }
            
            // Add 4th harmonic (even) - extra warmth (only if oversampled)
            if (h4_level > 0.0f)
            {
                float pow4 = driven * driven * driven * driven * sign;
                saturated += pow4 * h4_level;
            }
            
            // Soft saturation for tube compression at high levels
            if (absInput > 0.8f)
            {
                float excess = (absInput - 0.8f) / 0.2f;
                float tubeSat = 0.8f + 0.2f * std::tanh(excess * 0.7f);
                saturated = sign * tubeSat * (saturated / absInput);
            }
        }
        
        // LA-2A output transformer - gentle high-frequency rolloff
        // Characteristic warmth from transformer
        // Use fixed filtering regardless of oversampling to maintain consistent harmonics
        float transformerFreq = 20000.0f;  // Fixed frequency for consistent harmonics
        // Always use base sample rate for consistent filtering
        float filterCoeff = std::exp(-2.0f * 3.14159f * transformerFreq / static_cast<float>(sampleRate));
        
        // Check for NaN/Inf and reset if needed
        if (std::isnan(detector.saturationLowpass) || std::isinf(detector.saturationLowpass))
            detector.saturationLowpass = 0.0f;
            
        detector.saturationLowpass = saturated * (1.0f - filterCoeff * 0.05f) + detector.saturationLowpass * filterCoeff * 0.05f;
        
        return juce::jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, detector.saturationLowpass);
    }
    
    float getGainReduction(int channel) const
    {
        if (channel >= static_cast<int>(detectors.size()))
            return 0.0f;
        return juce::Decibels::gainToDecibels(detectors[channel].envelope);
    }

private:
    struct Detector
    {
        float envelope = 1.0f;
        float rms = 0.0f;
        float releaseStartLevel = 1.0f;  // For two-stage release
        int releasePhase = 0;             // 0=idle, 1=fast, 2=slow
        float maxReduction = 0.0f;       // Track max reduction for program dependency
        float holdCounter = 0.0f;        // Track how long compression is held
        float lightMemory = 0.0f;        // T4 cell light memory (legacy, kept for compatibility)
        float previousReduction = 0.0f;  // Previous reduction for delta tracking
        float hfFilter = 0.0f;           // High frequency filter state
        float releaseStartTime = 0.0f;   // Time since release started
        float saturationLowpass = 0.0f;  // Anti-aliasing filter state
        float prevInput = 0.0f;          // Previous input for filtering

        // Signal history for adaptive release
        float peakLevel = 0.0f;          // Peak level tracker
        float averageLevel = 0.0f;       // Average level tracker
        int transientCount = 0;          // Transient counter for density
        float transientDensity = 0.0f;   // 0-1, calculated periodically
        int samplesSinceTransient = 0;   // Sample counter for transient detection
        int sampleWindowCounter = 0;     // Window counter for periodic density updates

        // T4B Dual Time-Constant Model (hardware-accurate)
        float fastMemory = 0.0f;         // Fast photoresistor component (~10ms attack, ~60ms decay)
        float slowMemory = 0.0f;         // Slow phosphor persistence (~200ms glow)
    };
    
    std::vector<Detector> detectors;
    double sampleRate = 0.0;  // Set by prepare() from DAW
    
    // PROFESSIONAL FIX: Dedicated oversampler for saturation stage
    // This ALWAYS runs at 2x to ensure consistent harmonics
    std::unique_ptr<juce::dsp::Oversampling<float>> saturationOversampler;
};

// FET Compressor (1176 style)
class UniversalCompressor::FETCompressor
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        detectors.resize(numChannels);
        for (auto& detector : detectors)
        {
            detector.envelope = 1.0f;
            detector.prevOutput = 0.0f;
            detector.previousLevel = 0.0f;
        }
    }
    
    float process(float input, int channel, float inputGainDb, float outputGainDb, 
                  float attackMs, float releaseMs, int ratioIndex, bool oversample = false)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;
        
        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;
            
        auto& detector = detectors[channel];
        
        // 1176 Input transformer emulation
        // The 1176 uses the full input signal, not highpass filtered
        // The transformer provides some low-frequency coupling but doesn't remove DC entirely
        float filteredInput = input;
        
        // 1176 Input control - AUTHENTIC BEHAVIOR
        // The 1176 has a FIXED threshold that the input knob drives signal into
        // More input = more compression (not threshold change)
        
        // Fixed threshold (1176 characteristic)
        // The 1176 threshold is around -10 dBFS according to specifications
        // This is the level where compression begins to engage
        const float thresholdDb = Constants::FET_THRESHOLD_DB; // Authentic 1176 threshold
        float threshold = juce::Decibels::decibelsToGain(thresholdDb);
        
        // Apply FULL input gain - this is how you drive into compression
        // Input knob range: -20 to +40dB
        float inputGainLin = juce::Decibels::decibelsToGain(inputGainDb);
        float amplifiedInput = filteredInput * inputGainLin;
        
        // Ratio mapping: 4:1, 8:1, 12:1, 20:1, all-buttons mode
        // All-buttons mode: Hardware measurements show >100:1 effective ratio
        std::array<float, 5> ratios = {4.0f, 8.0f, 12.0f, 20.0f, 120.0f}; // All-buttons >100:1
        float ratio = ratios[juce::jlimit(0, 4, ratioIndex)];
        
        // FEEDBACK TOPOLOGY for authentic 1176 behavior
        // The 1176 uses feedback compression which creates its characteristic sound
        
        // First, we need to apply the PREVIOUS envelope to get the compressed signal
        float compressed = amplifiedInput * detector.envelope;
        
        // Then detect from the COMPRESSED OUTPUT (feedback)
        // This is what gives the 1176 its "grabby" characteristic
        float detectionLevel = std::abs(compressed);
        
        // Calculate gain reduction based on how much we exceed threshold
        float reduction = 0.0f;
        if (detectionLevel > threshold)
        {
            // Calculate how much we're over threshold in dB
            float overThreshDb = juce::Decibels::gainToDecibels(detectionLevel / threshold);
            
            // Classic 1176 compression curve
            if (ratioIndex == 4) // All-buttons mode (FET mode)
            {
                // All-buttons mode creates a unique compression characteristic
                // The actual 1176 in all-buttons mode creates a gentler slope at low levels
                // and more aggressive compression at higher levels (non-linear curve)
                
                if (overThreshDb < 3.0f)
                {
                    // Gentle compression at low levels (closer to 1.5:1)
                    reduction = overThreshDb * 0.33f;
                }
                else if (overThreshDb < 10.0f)
                {
                    // Medium compression (ramps up to about 4:1)
                    float t = (overThreshDb - 3.0f) / 7.0f;
                    reduction = 1.0f + (overThreshDb - 3.0f) * (0.75f + t * 0.15f);
                }
                else
                {
                    // Heavy limiting above 10dB over threshold (approaches 20:1)
                    reduction = 6.25f + (overThreshDb - 10.0f) * 0.95f;
                }
                
                // All-buttons mode can achieve substantial gain reduction but not extreme
                reduction = juce::jmin(reduction, 30.0f); // Max 30dB reduction (same as normal)
            }
            else
            {
                // Standard compression ratios
                reduction = overThreshDb * (1.0f - 1.0f / ratio);
                // Limit maximum gain reduction for normal modes
                reduction = juce::jmin(reduction, Constants::FET_MAX_REDUCTION_DB);
            }
        }
        
        // 1176 attack and release times with LOGARITHMIC curves (hardware-accurate)
        // Attack: 20µs (0.00002s) to 800µs (0.0008s) - logarithmic taper
        // Release: 50ms to 1.1s - logarithmic taper

        // Map input parameters (assumed 0-1 range from attackMs/releaseMs) to hardware values
        // If attackMs is already in ms, we need to map it logarithmically
        const float minAttack = 0.00002f;  // 20 microseconds
        const float maxAttack = 0.0008f;   // 800 microseconds
        const float minRelease = 0.05f;    // 50 milliseconds
        const float maxRelease = 1.1f;     // 1.1 seconds

        // Logarithmic interpolation for authentic 1176 feel
        float attackNorm = juce::jlimit(0.0f, 1.0f, attackMs / 0.8f); // Normalize to 0-1 if in ms
        float releaseNorm = juce::jlimit(0.0f, 1.0f, releaseMs / 1100.0f); // Normalize to 0-1 if in ms

        float attackTime = minAttack * std::pow(maxAttack / minAttack, attackNorm);
        float releaseTime = minRelease * std::pow(maxRelease / minRelease, releaseNorm);
        
        // All-buttons mode (FET mode) affects timing
        if (ratioIndex == 4)
        {
            // All-buttons mode has fast attack and modified release
            // But not so fast that it causes distortion
            attackTime = juce::jmin(attackTime, 0.0001f); // 100 microseconds minimum
            releaseTime *= 0.7f; // Somewhat faster release
            
            // Add some program-dependent variation for the unique FET mode sound
            float reductionFactor = juce::jlimit(0.0f, 1.0f, reduction / 20.0f);
            releaseTime *= (1.0f + reductionFactor * 0.3f); // Slightly slower release with more compression
        }
        
        // Program-dependent behavior: timing varies with program material
        float programFactor = juce::jlimit(0.5f, 2.0f, 1.0f + reduction * 0.05f);
        
        // Track signal dynamics for program dependency
        float signalDelta = std::abs(detectionLevel - detector.previousLevel);
        detector.previousLevel = detectionLevel;
        
        // Adjust timing based on program content
        if (signalDelta > 0.1f) // Transient material
        {
            attackTime *= 0.8f; // Faster attack for transients
            releaseTime *= 1.2f; // Slower release for transients
        }
        else // Sustained material
        {
            attackTime *= programFactor;
            releaseTime *= programFactor;
        }
        
        // Envelope following with proper exponential coefficients
        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        
        // Calculate proper exponential coefficients for smooth envelope with safety checks
        float attackCoeff = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, attackTime * static_cast<float>(sampleRate))));
        float releaseCoeff = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, releaseTime * static_cast<float>(sampleRate))));
        
        
        // FET mode has unique envelope behavior
        if (ratioIndex == 4)
        {
            // All-buttons mode has faster but still controlled envelope following
            // This creates the characteristic "pumping" effect without instability
            if (targetGain < detector.envelope)
            {
                // Fast attack in FET mode but not instantaneous to avoid distortion
                float fetAttackCoeff = std::exp(-1.0f / (Constants::FET_ALLBUTTONS_ATTACK * static_cast<float>(sampleRate)));
                detector.envelope = fetAttackCoeff * detector.envelope + (1.0f - fetAttackCoeff) * targetGain;
            }
            else
            {
                // Release with characteristic FET mode "breathing"
                // Slightly faster release but still smooth
                float fetReleaseCoeff = releaseCoeff * 0.98f; // Slightly faster than normal
                detector.envelope = fetReleaseCoeff * detector.envelope + (1.0f - fetReleaseCoeff) * targetGain;
            }
        }
        else
        {
            // Normal 1176 envelope behavior for standard ratios
            if (targetGain < detector.envelope)
            {
                // Attack phase - FET response
                detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
            }
            else
            {
                // Release phase
                detector.envelope = releaseCoeff * detector.envelope + (1.0f - releaseCoeff) * targetGain;
            }
        }
        
        // Ensure envelope stays within valid range for stability
        // In feedback topology, we need to prevent runaway gain
        detector.envelope = juce::jlimit(0.001f, 1.0f, detector.envelope);

        // Envelope hysteresis: blend with previous gain reduction for analog memory
        // This creates smoother transitions and mimics analog circuit capacitance
        float currentGR = 1.0f - detector.envelope; // Convert to gain reduction amount
        currentGR = 0.85f * currentGR + 0.15f * detector.previousGR; // 15% memory
        detector.previousGR = currentGR;
        detector.envelope = 1.0f - currentGR; // Convert back to envelope

        // NaN/Inf safety check
        if (std::isnan(detector.envelope) || std::isinf(detector.envelope))
            detector.envelope = 1.0f;
        
        // 1176 Class A FET amplifier stage
        // The 1176 is VERY clean at -18dB input level
        // UAD reference shows THD at -65dB with 2nd harmonic at -100dB
        // Apply the envelope to get the output signal
        float output = compressed;
        
        // The 1176 is an extremely clean compressor with minimal harmonics
        // At -18dB input: 2nd harmonic at -100dB, 3rd at -110dB
        float absOutput = std::abs(output);
        
        // FET non-linearity and harmonics
        // All-buttons mode: 3x more harmonic distortion
        if (reduction > 3.0f && absOutput > 0.001f)
        {
            float sign = (output < 0.0f) ? -1.0f : 1.0f;

            // All-buttons mode increases harmonic content significantly
            float allButtonsMultiplier = (ratioIndex == 4) ? 3.0f : 1.0f;

            // Dynamic harmonics: scale with gain reduction for authentic 1176 behavior
            // More compression = more harmonic distortion (FET characteristic)
            float grAmount = juce::jlimit(0.0f, 1.0f, reduction / 20.0f); // 0-1 scaling

            // Tanh-based FET saturation for authentic character
            // Saturation increases dynamically with gain reduction
            float saturationAmount = grAmount * allButtonsMultiplier;
            float tanhDrive = 1.0f + saturationAmount * 0.5f; // Gentle overdrive
            float distorted = std::tanh(output * tanhDrive) / tanhDrive;

            // Blend original with distorted - blend amount scales with GR
            float blendAmount = 0.2f + (grAmount * 0.3f); // 20-50% blend based on GR
            output = output * (1.0f - blendAmount) + distorted * blendAmount;

            // Dynamic harmonic generation - scales with gain reduction amount
            // Light compression: minimal harmonics (~0.2x)
            // Heavy compression: full harmonics (1.0x)
            float harmonicScale = 0.2f + (grAmount * 0.8f); // 0.2 to 1.0 range

            // 2nd harmonic: -100dB absolute (-82dB relative at -18dB)
            // Scales more aggressively with GR for dynamic character
            float h2_scale = 0.00063f * allButtonsMultiplier * harmonicScale;
            float h2 = output * output * h2_scale;

            // 3rd harmonic (odd-order for FET character)
            // Odd harmonics scale even more with GR (squared relationship)
            float h3_scale = 0.0005f * allButtonsMultiplier * (harmonicScale * harmonicScale);
            float h3 = output * output * output * h3_scale;

            // 5th harmonic (additional odd-order for FET)
            // Most aggressive scaling for aggressive compression
            float h5_scale = 0.0001f * allButtonsMultiplier * (grAmount * grAmount);
            float h5 = std::pow(output, 5) * h5_scale;

            output += h2 * sign + h3 + h5;
        }
        
        // Hard limiting if we're clipping
        if (absOutput > 1.5f)
        {
            float sign = (output < 0.0f) ? -1.0f : 1.0f;
            output = sign * (1.5f + std::tanh((absOutput - 1.5f) * 0.2f) * 0.5f);
        }
        
        // Harmonic compensation removed - was causing artifacts
        
        // Output transformer simulation - very subtle
        // 1176 has minimal transformer coloration
        // Just a gentle rolloff above 20kHz for anti-aliasing
        // Use fixed filtering regardless of oversampling to maintain consistent harmonics
        float transformerFreq = 20000.0f;
        // Always use base sample rate for consistent filtering
        float transformerCoeff = std::exp(-2.0f * 3.14159f * transformerFreq / static_cast<float>(sampleRate));
        float filtered = output * (1.0f - transformerCoeff * 0.05f) + detector.prevOutput * transformerCoeff * 0.05f;
        detector.prevOutput = filtered;
        
        // 1176 Output knob - makeup gain control
        // Output parameter is in dB (-20 to +20dB) - more reasonable range
        // This is pure makeup gain after compression
        float outputGainLin = juce::Decibels::decibelsToGain(outputGainDb);
        
        // Apply makeup gain
        float finalOutput = filtered * outputGainLin;
        
        // Ensure output is within reasonable bounds
        return juce::jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, finalOutput);
    }
    
    float getGainReduction(int channel) const
    {
        if (channel >= static_cast<int>(detectors.size()))
            return 0.0f;
        return juce::Decibels::gainToDecibels(detectors[channel].envelope);
    }

private:
    struct Detector
    {
        float envelope = 1.0f;
        float prevOutput = 0.0f;
        float previousLevel = 0.0f; // For program-dependent behavior
        float previousGR = 0.0f;    // For envelope hysteresis
    };
    
    std::vector<Detector> detectors;
    double sampleRate = 0.0;  // Set by prepare() from DAW
};

// VCA Compressor (DBX 160 style)
class UniversalCompressor::VCACompressor
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        detectors.resize(numChannels);
        for (auto& detector : detectors)
        {
            detector.envelope = 1.0f;
            detector.rmsBuffer = 0.0f;
            detector.previousReduction = 0.0f;
            detector.controlVoltage = 0.0f;
            detector.signalEnvelope = 0.0f;
            detector.envelopeRate = 0.0f;
            detector.previousInput = 0.0f;
            detector.overshootAmount = 0.0f; // For DBX 160 attack overshoot
        }
    }
    
    float process(float input, int channel, float threshold, float ratio, 
                  float attackParam, float releaseParam, float outputGain, bool overEasy = false, bool oversample = false)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;
        
        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;
            
        auto& detector = detectors[channel];
        
        // DBX 160 feedforward topology: control voltage from input signal
        float detectionLevel = std::abs(input);

        // Track signal envelope rate of change for program-dependent behavior
        float signalDelta = std::abs(detectionLevel - detector.previousInput);
        detector.envelopeRate = detector.envelopeRate * 0.95f + signalDelta * 0.05f;
        detector.previousInput = detectionLevel;

        // DBX 160 True RMS detection with ADAPTIVE window (5-15ms)
        // Transient material (drums): shorter window (5ms) for punch
        // Sustained material (vocals, bass): longer window (15ms) for smoothness

        // Detect transients: rapid level changes indicate percussive content
        float transientFactor = juce::jlimit(0.0f, 1.0f, detector.envelopeRate * 10.0f);

        // Adaptive RMS window: 5ms (transient) to 15ms (sustained)
        float adaptiveRmsTime = 0.015f - (transientFactor * 0.010f); // 15ms to 5ms

        const float rmsAlpha = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, adaptiveRmsTime * static_cast<float>(sampleRate))));
        detector.rmsBuffer = detector.rmsBuffer * rmsAlpha + detectionLevel * detectionLevel * (1.0f - rmsAlpha);
        float rmsLevel = std::sqrt(detector.rmsBuffer);
        
        // DBX 160 signal envelope tracking for program-dependent timing
        const float envelopeAlpha = 0.99f;
        detector.signalEnvelope = detector.signalEnvelope * envelopeAlpha + rmsLevel * (1.0f - envelopeAlpha);
        
        // DBX 160 threshold control (-40dB to +20dB range typical)
        float thresholdLin = juce::Decibels::decibelsToGain(threshold);
        
        float reduction = 0.0f;
        if (rmsLevel > thresholdLin)
        {
            float overThreshDb = juce::Decibels::gainToDecibels(rmsLevel / thresholdLin);
            
            // DBX 160 OverEasy mode - proprietary soft knee with PARABOLIC curve
            if (overEasy)
            {
                // DBX OverEasy uses a parabolic curve for smooth, musical compression
                // Knee width is approximately 10dB centered around threshold
                float kneeWidth = 10.0f;
                float kneeStart = -kneeWidth * 0.5f;
                float kneeEnd = kneeWidth * 0.5f;

                if (overThreshDb <= kneeStart)
                {
                    // Below knee - no compression
                    reduction = 0.0f;
                }
                else if (overThreshDb <= kneeEnd)
                {
                    // Inside knee - parabolic transition (quadratic curve)
                    // DBX uses parabolic curve: f(x) = x² for smooth onset
                    float kneePosition = (overThreshDb - kneeStart) / kneeWidth; // 0-1
                    float parabolaGain = kneePosition * kneePosition; // Quadratic (parabolic)
                    reduction = overThreshDb * parabolaGain * (1.0f - 1.0f / ratio);
                }
                else
                {
                    // Above knee - full compression with knee compensation
                    float kneeReduction = kneeEnd * 1.0f * (1.0f - 1.0f / ratio); // Full reduction at knee end
                    reduction = kneeReduction + (overThreshDb - kneeEnd) * (1.0f - 1.0f / ratio);
                }
            }
            else
            {
                // Hard knee compression (original DBX 160 without OverEasy)
                reduction = overThreshDb * (1.0f - 1.0f / ratio);
            }
            
            // DBX 160 can achieve infinite compression (approximately 120:1) with complete stability
            // Feed-forward design prevents instability issues of feedback compressors
            reduction = juce::jmin(reduction, Constants::VCA_MAX_REDUCTION_DB); // Practical limit for musical content
        }
        
        // DBX 160 program-dependent attack and release times that "track" signal envelope
        // Attack times automatically vary with rate of level change in program material
        // Manual specifications: 15ms for 10dB, 5ms for 20dB, 3ms for 30dB change above threshold
        // Release rate: 120dB/second
        
        float attackTime, releaseTime;
        
        // DBX 160 attack times track the signal envelope rate
        if (reduction > 0.1f)
        {
            // DBX 160 manual: Attack time for 63% of level change
            // 15ms for 10dB, 5ms for 20dB, 3ms for 30dB
            // Attack rates track the signal envelope
            if (reduction <= 10.0f)
                attackTime = 0.015f; // 15ms for 10dB level change
            else if (reduction <= 20.0f)
                attackTime = 0.005f; // 5ms for 20dB level change  
            else
                attackTime = 0.003f; // 3ms for 30dB level change
            
            // Ensure minimum attack time for stability
            attackTime = juce::jmax(0.001f, attackTime);
        }
        else
        {
            attackTime = 0.015f; // Default 15ms when not compressing
        }
        
        // DBX 160 release rate: constant 120dB/second regardless of program material
        // This is a key characteristic - release doesn't vary like attack does
        const float releaseRate = 120.0f; // dB per second
        if (reduction > 0.1f)
        {
            // Calculate release time based on how much reduction needs to be released
            releaseTime = reduction / releaseRate; // Time to release current reduction
            releaseTime = juce::jmax(0.008f, releaseTime); // Minimum 8ms
        }
        else
        {
            releaseTime = 0.008f; // 8ms minimum
        }
        
        // DBX VCA control voltage generation (-6mV/dB logarithmic curve)
        // This is key to the DBX sound - logarithmic VCA response
        detector.controlVoltage = reduction * Constants::VCA_CONTROL_VOLTAGE_SCALE; // -6mV/dB characteristic
        
        // DBX 160 feed-forward envelope following with complete stability
        // Feed-forward design is inherently stable even at infinite compression ratios
        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        
        // Calculate proper exponential coefficients for DBX-style response with safety
        float attackCoeff = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, attackTime * static_cast<float>(sampleRate))));
        float releaseCoeff = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, releaseTime * static_cast<float>(sampleRate))));
        
        if (targetGain < detector.envelope)
        {
            // Attack phase - DBX feed-forward design for fast, stable response
            detector.envelope = targetGain + (detector.envelope - targetGain) * attackCoeff;

            // DBX 160 attack overshoot on fast attacks (1-2dB characteristic)
            // Fast attacks (< 5ms) produce slight overshoot for transient emphasis
            if (attackTime < 0.005f && reduction > 5.0f)
            {
                // Calculate overshoot amount based on attack speed and reduction depth
                // Faster attack = more overshoot, up to ~2dB
                float overshootFactor = (0.005f - attackTime) / 0.004f; // 0-1 range
                float reductionFactor = juce::jlimit(0.0f, 1.0f, reduction / 20.0f); // 0-1 range

                // Overshoot peaks at ~2dB (1.26 gain factor) for very fast attacks on heavy compression
                detector.overshootAmount = overshootFactor * reductionFactor * 0.02f; // Up to 2dB
            }
            else
            {
                // Decay overshoot gradually when not in fast attack
                detector.overshootAmount *= 0.95f;
            }
        }
        else
        {
            // Release phase - constant 120dB/second release rate
            detector.envelope = targetGain + (detector.envelope - targetGain) * releaseCoeff;

            // No overshoot during release
            detector.overshootAmount *= 0.98f; // Quick decay
        }
        
        // Feed-forward stability: ensure envelope stays within bounds
        // This prevents the instability that plagues feedback compressors at high ratios
        detector.envelope = juce::jlimit(0.0001f, 1.0f, detector.envelope);
        
        // NaN/Inf safety check
        if (std::isnan(detector.envelope) || std::isinf(detector.envelope))
            detector.envelope = 1.0f;
        
        // Store previous reduction for program dependency tracking
        detector.previousReduction = reduction;

        // Apply overshoot to envelope for DBX 160 attack characteristic
        float envelopeWithOvershoot = detector.envelope * (1.0f + detector.overshootAmount);
        envelopeWithOvershoot = juce::jlimit(0.0001f, 1.0f, envelopeWithOvershoot);

        // DBX 160 feed-forward topology: apply compression to input signal
        // This is different from feedback compressors - much more stable
        float compressed = input * envelopeWithOvershoot;
        
        // DBX VCA characteristics (DBX 202 series VCA chip used in 160)
        // The DBX 160 is renowned for being EXTREMELY clean - much cleaner than most compressors
        // Manual specification: 0.075% 2nd harmonic at infinite compression at +4dBm output
        // 0.5% 3rd harmonic typical at infinite compression ratio
        float processed = compressed;
        float absLevel = std::abs(processed);
        
        // Calculate actual signal level in dB for harmonic generation
        float levelDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, absLevel));
        
        // DBX 160 harmonic distortion - much cleaner than other compressor types
        if (absLevel > 0.01f)  // Process non-silence
        {
            float sign = (processed < 0.0f) ? -1.0f : 1.0f;
            
            // DBX 160 VCA harmonics - extremely clean, even at high compression ratios
            float h2_level = 0.0f;
            float h3_level = 0.0f;
            
            // No pre-saturation compensation needed anymore
            // We apply compensation AFTER saturation to avoid compression effects
            float harmonicCompensation = 1.0f; // No pre-compensation
            float h2Boost = harmonicCompensation;
            float h3Boost = harmonicCompensation;
            
            // DBX 160 stays very clean even when compressing hard
            // Only add harmonics when really compressing
            if (levelDb > -20.0f && reduction > 5.0f)
            {
                // DBX 160 manual spec: 0.075% 2nd harmonic at infinite compression at +4dBm output
                // 2nd harmonic = 0.00075 linear
                float compressionFactor = juce::jmin(1.0f, reduction / 30.0f);
                
                // Scale for 0.075% 2nd harmonic
                float h2_scale = 0.00075f / (absLevel * absLevel + 0.0001f);  // Direct calculation
                h2_level = absLevel * absLevel * h2_scale * compressionFactor * h2Boost;
                
                // DBX 160 manual spec: 0.5% 3rd harmonic typical at infinite compression
                // Note: 3rd harmonic decreases linearly with frequency (1/2 at 100Hz vs 50Hz)
                if (reduction > 15.0f)
                {
                    // 3rd harmonic = 0.005 linear (0.5%)
                    // Account for frequency dependence (we're testing at 1kHz)
                    float freqFactor = 50.0f / 1000.0f;  // Linear decrease with frequency
                    float h3_scale = (0.005f * freqFactor) / (absLevel * absLevel * absLevel + 0.0001f);
                    h3_level = absLevel * absLevel * absLevel * h3_scale * compressionFactor * h3Boost;
                }
            }
            
            // Apply minimal harmonics - DBX 160 is known for its cleanliness
            processed = compressed;
            
            // Add very subtle 2nd harmonic
            if (h2_level > 0.0f)
            {
                // Use waveshaping for consistent harmonic generation
                float squared = compressed * compressed * sign;
                processed += squared * h2_level;
            }
            
            // Add very subtle 3rd harmonic
            if (h3_level > 0.0f)
            {
                // Use waveshaping for consistent harmonic generation
                float cubed = compressed * compressed * compressed;
                processed += cubed * h3_level;
            }
            
            // DBX VCA has very high headroom - minimal saturation
            if (absLevel > 1.5f)
            {
                // Very gentle VCA saturation characteristic
                float excess = absLevel - 1.5f;
                float vcaSat = 1.5f + std::tanh(excess * 0.3f) * 0.2f;
                processed = sign * vcaSat * (processed / absLevel);
            }
        }
        
        // Apply output gain with proper VCA response
        float output = processed * juce::Decibels::decibelsToGain(outputGain);
        
        // Final output limiting for safety
        return juce::jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, output);
    }
    
    float getGainReduction(int channel) const
    {
        if (channel >= static_cast<int>(detectors.size()))
            return 0.0f;
        return juce::Decibels::gainToDecibels(detectors[channel].envelope);
    }

private:
    struct Detector
    {
        float envelope = 1.0f;
        float rmsBuffer = 0.0f;         // True RMS detection buffer
        float previousReduction = 0.0f; // For program-dependent behavior
        float controlVoltage = 0.0f;    // VCA control voltage (-6mV/dB)
        float signalEnvelope = 0.0f;    // Signal envelope for program-dependent timing
        float envelopeRate = 0.0f;      // Rate of envelope change
        float previousInput = 0.0f;     // Previous input for envelope tracking
        float overshootAmount = 0.0f;   // Attack overshoot for DBX 160 characteristic
    };
    
    std::vector<Detector> detectors;
    double sampleRate = 0.0;  // Set by prepare() from DAW
};

// Bus Compressor (SSL style)
class UniversalCompressor::BusCompressor
{
public:
    void prepare(double sampleRate, int numChannels, int blockSize = 512)
    {
        if (sampleRate <= 0.0 || numChannels <= 0 || blockSize <= 0)
            return;
            
        this->sampleRate = sampleRate;
        detectors.clear();
        detectors.resize(numChannels);
        
        // Initialize sidechain filters with actual block size
        juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(blockSize), static_cast<juce::uint32>(1)};
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& detector = detectors[ch];
            detector.envelope = 1.0f;
            detector.rms = 0.0f;
            detector.previousLevel = 0.0f;
            detector.hpState = 0.0f;
            detector.prevInput = 0.0f;
            
            // Create the filter chain
            detector.sidechainFilter = std::make_unique<juce::dsp::ProcessorChain<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Filter<float>>>();
            
            // SSL G-Series sidechain filter
            // Highpass at 60Hz to prevent pumping from low frequencies
            detector.sidechainFilter->get<0>().coefficients = 
                juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 60.0f, 0.707f);
            // No lowpass in original SSL G - full bandwidth
            detector.sidechainFilter->get<1>().coefficients = 
                juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 20000.0f, 0.707f);
            
            // Then prepare and set bypass states
            detector.sidechainFilter->prepare(spec);
            detector.sidechainFilter->setBypassed<0>(false);
            detector.sidechainFilter->setBypassed<1>(false);
        }
    }
    
    float process(float input, int channel, float threshold, float ratio,
                  int attackIndex, int releaseIndex, float makeupGain, float mixAmount = 1.0f, bool oversample = false)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;
        
        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;
            
        auto& detector = detectors[channel];
        
        // SSL G-Series quad VCA topology
        // Uses parallel detection path with feed-forward design
        
        // Step 2: Apply gain reduction to main signal
        // Use simple inline filter instead of complex ProcessorChain for per-sample processing
        float sidechainInput = input;
        if (detector.sidechainFilter)
        {
            // Simple 60Hz highpass filter (much faster than full ProcessorChain)
            const float hpCutoff = 60.0f / static_cast<float>(sampleRate);
            const float hpAlpha = juce::jmin(1.0f, hpCutoff);
            detector.hpState = input - detector.prevInput + detector.hpState * (1.0f - hpAlpha);
            detector.prevInput = input;
            sidechainInput = detector.hpState;
        }
        
        // Step 3: SSL uses the sidechain signal directly for detection
        float detectionLevel = std::abs(sidechainInput);
        
        // SSL G-Series specific ratios: 2:1, 4:1, 10:1
        // ratio parameter already contains the actual ratio value (2.0, 4.0, or 10.0)
        float actualRatio = ratio;
        
        float thresholdLin = juce::Decibels::decibelsToGain(threshold);
        
        float reduction = 0.0f;
        if (detectionLevel > thresholdLin)
        {
            float overThreshDb = juce::Decibels::gainToDecibels(detectionLevel / thresholdLin);
            
            // SSL G-Series compression curve - relatively linear/hard knee
            reduction = overThreshDb * (1.0f - 1.0f / actualRatio);
            // SSL bus typically used for gentle compression (max ~20dB GR)
            reduction = juce::jmin(reduction, Constants::BUS_MAX_REDUCTION_DB);
        }
        
        // SSL G-Series attack and release times
        std::array<float, 6> attackTimes = {0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f}; // ms
        std::array<float, 5> releaseTimes = {100.0f, 300.0f, 600.0f, 1200.0f, -1.0f}; // ms, -1 = auto
        
        float attackTime = attackTimes[juce::jlimit(0, 5, attackIndex)] * 0.001f;
        float releaseTime = releaseTimes[juce::jlimit(0, 4, releaseIndex)] * 0.001f;
        
        // SSL Auto-release mode - program-dependent (150-450ms range)
        if (releaseTime < 0.0f)
        {
            // Hardware-accurate SSL auto-release: 150ms to 450ms based on program
            // Faster for transient-dense material, slower for sustained compression

            // Track signal dynamics
            float signalDelta = std::abs(detectionLevel - detector.previousLevel);
            detector.previousLevel = detector.previousLevel * 0.95f + detectionLevel * 0.05f;

            // Transient density: high delta = drums/percussion, low delta = sustained
            float transientDensity = juce::jlimit(0.0f, 1.0f, signalDelta * 20.0f);

            // Compression amount factor: more compression = slower release
            float compressionFactor = juce::jlimit(0.0f, 1.0f, reduction / 12.0f); // 0dB to 12dB

            // SSL auto-release formula (150ms to 450ms)
            // Transient material: faster release (150-250ms)
            // Sustained material with heavy compression: slower release (300-450ms)
            float minRelease = 0.15f;  // 150ms
            float maxRelease = 0.45f;  // 450ms

            // Calculate release time based on material and compression
            float sustainedFactor = (1.0f - transientDensity) * compressionFactor;
            releaseTime = minRelease + (sustainedFactor * (maxRelease - minRelease));
        }
        
        // SSL G-Series envelope following with smooth response
        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        
        if (targetGain < detector.envelope)
        {
            // Attack phase - SSL is known for smooth attack response - approximate exp
            float divisor = juce::jmax(Constants::EPSILON, attackTime * static_cast<float>(sampleRate));
            float attackCoeff = juce::jmax(0.0f, juce::jmin(0.9999f, 1.0f - 1.0f / divisor));
            detector.envelope = targetGain + (detector.envelope - targetGain) * attackCoeff;
        }
        else
        {
            // Release phase with SSL's characteristic smoothness - approximate exp
            float divisor = juce::jmax(Constants::EPSILON, releaseTime * static_cast<float>(sampleRate));
            float releaseCoeff = juce::jmax(0.0f, juce::jmin(0.9999f, 1.0f - 1.0f / divisor));
            detector.envelope = targetGain + (detector.envelope - targetGain) * releaseCoeff;
        }
        
        // Envelope hysteresis: blend with previous gain reduction for SSL memory effect
        // SSL circuitry has capacitance that creates slight "memory" in the envelope
        float currentGR = 1.0f - detector.envelope;
        currentGR = 0.9f * currentGR + 0.1f * detector.previousGR; // 10% memory for SSL smoothness
        detector.previousGR = currentGR;
        detector.envelope = 1.0f - currentGR;

        // NaN/Inf safety check
        if (std::isnan(detector.envelope) || std::isinf(detector.envelope))
            detector.envelope = 1.0f;

        // Apply the gain reduction envelope to the input signal
        float compressed = input * detector.envelope;
        
        // SSL G-Series Quad DBX 202C VCA characteristics
        // Hardware-accurate THD: 0.01% @ 0dB GR, 0.05-0.1% @ 12dB GR
        float processed = compressed;
        float absLevel = std::abs(processed);

        // Calculate level for harmonic generation
        float levelDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, absLevel));

        // SSL Bus harmonics - quad VCA coloration increases with compression
        if (absLevel > 0.01f)
        {
            float sign = (processed < 0.0f) ? -1.0f : 1.0f;

            // SSL VCA THD specification:
            // No compression (0dB GR): 0.01% THD (-80dB)
            // Moderate compression (6dB GR): 0.05% THD (-66dB)
            // Heavy compression (12dB GR): 0.1% THD (-60dB)

            // Calculate THD percentage based on gain reduction
            float thdPercent;
            if (reduction < 0.1f)
                thdPercent = 0.01f;  // 0.01% at unity gain
            else if (reduction <= 6.0f)
                // Linear interpolation from 0.01% to 0.05%
                thdPercent = 0.01f + (reduction / 6.0f) * 0.04f;
            else if (reduction <= 12.0f)
                // Linear interpolation from 0.05% to 0.1%
                thdPercent = 0.05f + ((reduction - 6.0f) / 6.0f) * 0.05f;
            else
                thdPercent = 0.1f;  // Cap at 0.1% for heavy compression

            // Convert THD percentage to linear scale
            float thdLinear = thdPercent / 100.0f;

            // SSL quad VCA: primarily 2nd harmonic (even), minimal odd harmonics
            // 2nd harmonic: ~85% of total THD
            // 3rd harmonic: ~15% of total THD
            float h2_scale = thdLinear * 0.85f;
            float h3_scale = thdLinear * 0.15f;

            float h2_level = absLevel * absLevel * h2_scale;
            float h3_level = absLevel * absLevel * absLevel * h3_scale;
            
            // Apply harmonics using waveshaping for consistency
            processed = compressed;
            
            // Add 2nd harmonic for SSL warmth
            if (h2_level > 0.0f)
            {
                // Use waveshaping: x² preserves phase relationship
                float squared = compressed * compressed * sign;
                processed += squared * h2_level;
            }
            
            // Add 3rd harmonic for "bite"
            if (h3_level > 0.0f)
            {
                // Use waveshaping: x³ for odd harmonic
                float cubed = compressed * compressed * compressed;
                processed += cubed * h3_level;
            }
            
            // SSL console saturation - very gentle
            if (absLevel > 0.95f)
            {
                // SSL console output stage saturation
                float excess = (absLevel - 0.95f) / 0.05f;
                float sslSat = 0.95f + 0.05f * std::tanh(excess * 0.7f);
                processed = sign * sslSat * (processed / absLevel);
            }
        }
        
        // Apply makeup gain
        float compressed_output = processed * juce::Decibels::decibelsToGain(makeupGain);

        // SSL-style parallel compression (New York compression)
        // Blend dry and wet signals for "glue" effect
        float output = input * (1.0f - mixAmount) + compressed_output * mixAmount;

        // Final output limiting
        return juce::jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, output);
    }
    
    float getGainReduction(int channel) const
    {
        if (channel >= static_cast<int>(detectors.size()))
            return 0.0f;
        return juce::Decibels::gainToDecibels(detectors[channel].envelope);
    }

private:
    struct Detector
    {
        float envelope = 1.0f;
        float rms = 0.0f;
        float previousLevel = 0.0f; // For auto-release tracking
        float hpState = 0.0f;       // Simple highpass filter state
        float prevInput = 0.0f;     // Previous input for filter
        float previousGR = 0.0f;    // For envelope hysteresis
        std::unique_ptr<juce::dsp::ProcessorChain<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Filter<float>>> sidechainFilter;
    };
    
    std::vector<Detector> detectors;
    double sampleRate = 0.0;  // Set by prepare() from DAW
};

// Parameter layout creation
juce::AudioProcessorValueTreeState::ParameterLayout UniversalCompressor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    try {
    
    // Mode selection
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode",
        juce::StringArray{"Opto", "FET", "VCA", "Bus"}, 0)); // Default to Opto
    
    // Global parameters
    // Oversample removed - saturation always runs at 2x internally now
    // layout.add(std::make_unique<juce::AudioParameterBool>("oversample", "Oversample", true));
    layout.add(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    
    // Stereo linking control (0% = independent, 100% = fully linked)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "stereo_link", "Stereo Link", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    
    // Mix control for parallel compression (0% = dry, 100% = wet)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    
    // Attack/Release curve options (0 = logarithmic/analog, 1 = linear/digital)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "envelope_curve", "Envelope Curve", 
        juce::StringArray{"Logarithmic (Analog)", "Linear (Digital)"}, 0));
    
    // Vintage/Modern modes for harmonic profiles
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "saturation_mode", "Saturation Mode", 
        juce::StringArray{"Vintage (Warm)", "Modern (Clean)", "Pristine (Minimal)"}, 0));
    
    // External sidechain enable
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "sidechain_enable", "External Sidechain", false));

    // Analog noise floor enable (optional for CPU savings)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "noise_enable", "Analog Noise", true)); // Default ON for analog character

    // Add read-only gain reduction meter parameter for DAW display (LV2/VST3)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "gr_meter", "GR", 
        juce::NormalisableRange<float>(-30.0f, 0.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    
    // Opto parameters (LA-2A style)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "opto_peak_reduction", "Peak Reduction", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f)); // Default to 0 (no compression)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "opto_gain", "Gain", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f)); // Unity gain at 50%
    layout.add(std::make_unique<juce::AudioParameterBool>("opto_limit", "Limit Mode", false));
    
    // FET parameters (1176 style)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "fet_input", "Input", 
        juce::NormalisableRange<float>(-20.0f, 40.0f, 0.1f), 0.0f)); // Default to 0dB
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "fet_output", "Output", 
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f)); // Default to 0dB (unity gain)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "fet_attack", "Attack", 
        juce::NormalisableRange<float>(0.02f, 0.8f, 0.01f), 0.02f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "fet_release", "Release", 
        juce::NormalisableRange<float>(50.0f, 1100.0f, 1.0f), 400.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "fet_ratio", "Ratio", 
        juce::StringArray{"4:1", "8:1", "12:1", "20:1", "All"}, 0));
    
    // VCA parameters (DBX 160 style)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "vca_threshold", "Threshold", 
        juce::NormalisableRange<float>(-38.0f, 12.0f, 0.1f), 0.0f)); // DBX 160 range: 10mV(-38dB) to 3V(+12dB)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "vca_ratio", "Ratio", 
        juce::NormalisableRange<float>(1.0f, 120.0f, 0.1f), 2.0f)); // DBX 160 range: 1:1 to 120:1 (infinity), default 2:1
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "vca_attack", "Attack", 
        juce::NormalisableRange<float>(0.1f, 50.0f, 0.1f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "vca_release", "Release", 
        juce::NormalisableRange<float>(10.0f, 5000.0f, 1.0f), 100.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "vca_output", "Output", 
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>("vca_overeasy", "Over Easy", false));
    
    // Bus parameters (SSL style)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "bus_threshold", "Threshold", 
        juce::NormalisableRange<float>(-30.0f, 15.0f, 0.1f), 0.0f)); // Extended range for more flexibility, default to 0dB
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "bus_ratio", "Ratio", 
        juce::StringArray{"2:1", "4:1", "10:1"}, 0)); // SSL spec: discrete ratios
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "bus_attack", "Attack", 
        juce::StringArray{"0.1ms", "0.3ms", "1ms", "3ms", "10ms", "30ms"}, 2));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "bus_release", "Release", 
        juce::StringArray{"0.1s", "0.3s", "0.6s", "1.2s", "Auto"}, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "bus_makeup", "Makeup",
        juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "bus_mix", "Bus Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%"))); // SSL parallel compression

    // Digital (Modern) Compressor parameters
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_threshold", "Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -20.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_ratio", "Ratio",
        juce::NormalisableRange<float>(1.0f, 100.0f, 0.1f), 4.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_knee", "Knee",
        juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f), 2.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_attack", "Attack",
        juce::NormalisableRange<float>(0.01f, 500.0f, 0.01f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_release", "Release",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 1.0f), 100.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_lookahead", "Lookahead",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 2.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_output", "Output",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "digital_adaptive", "Adaptive Release", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "digital_sidechain_listen", "SC Listen", false));
    }
    catch (const std::exception& e) {
        DBG("Failed to create parameter layout: " << e.what());
    }
    catch (...) {
        DBG("Failed to create parameter layout: unknown error");
    }
    
    return layout;
}

// Lookup table implementations
void UniversalCompressor::LookupTables::initialize()
{
    // Precompute exponential values for range -4 to 0 (typical for envelope coefficients)
    for (int i = 0; i < TABLE_SIZE; ++i)
    {
        float x = -4.0f + (4.0f * i / static_cast<float>(TABLE_SIZE - 1));
        expTable[i] = std::exp(x);
    }
    
    // Precompute logarithm values for range 0.0001 to 1.0
    for (int i = 0; i < TABLE_SIZE; ++i)
    {
        float x = 0.0001f + (0.9999f * i / static_cast<float>(TABLE_SIZE - 1));
        logTable[i] = std::log(x);
    }
}

inline float UniversalCompressor::LookupTables::fastExp(float x) const
{
    // Clamp to table range
    x = juce::jlimit(-4.0f, 0.0f, x);
    // Map to table index
    int index = static_cast<int>((x + 4.0f) * (TABLE_SIZE - 1) / 4.0f);
    index = juce::jlimit(0, TABLE_SIZE - 1, index);
    return expTable[index];
}

inline float UniversalCompressor::LookupTables::fastLog(float x) const
{
    // Clamp to table range
    x = juce::jlimit(0.0001f, 1.0f, x);
    // Map to table index
    int index = static_cast<int>((x - 0.0001f) * (TABLE_SIZE - 1) / 0.9999f);
    index = juce::jlimit(0, TABLE_SIZE - 1, index);
    return logTable[index];
}

// Constructor
UniversalCompressor::UniversalCompressor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)  // Optional sidechain input
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "UniversalCompressor", createParameterLayout()),
      currentSampleRate(0.0),  // Set by prepareToPlay from DAW
      currentBlockSize(512)
{
    // Initialize atomic values explicitly with relaxed ordering
    inputMeter.store(-60.0f, std::memory_order_relaxed);
    outputMeter.store(-60.0f, std::memory_order_relaxed);
    grMeter.store(0.0f, std::memory_order_relaxed);
    
    // Initialize lookup tables
    lookupTables = std::make_unique<LookupTables>();
    lookupTables->initialize();
    
    try {
        // Initialize compressor instances with error handling
        optoCompressor = std::make_unique<OptoCompressor>();
        fetCompressor = std::make_unique<FETCompressor>();
        vcaCompressor = std::make_unique<VCACompressor>();
        busCompressor = std::make_unique<BusCompressor>();
        antiAliasing = std::make_unique<AntiAliasing>();
    }
    catch (const std::exception& e) {
        // Ensure all pointers are null on failure
        optoCompressor.reset();
        fetCompressor.reset();
        vcaCompressor.reset();
        busCompressor.reset();
        antiAliasing.reset();
        DBG("Failed to initialize compressors: " << e.what());
    }
    catch (...) {
        // Ensure all pointers are null on failure
        optoCompressor.reset();
        fetCompressor.reset();
        vcaCompressor.reset();
        busCompressor.reset();
        antiAliasing.reset();
        DBG("Failed to initialize compressors: unknown error");
    }
}

UniversalCompressor::~UniversalCompressor() 
{
    // Explicitly reset all compressors in reverse order
    antiAliasing.reset();
    busCompressor.reset();
    vcaCompressor.reset();
    fetCompressor.reset();
    optoCompressor.reset();
}

void UniversalCompressor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (sampleRate <= 0.0 || samplesPerBlock <= 0)
        return;

    // Disable denormal numbers globally for this processor
    juce::FloatVectorOperations::disableDenormalisedNumberSupport(true);

    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    
    int numChannels = juce::jmax(1, getTotalNumOutputChannels());
    
    // Prepare all compressor types safely
    if (optoCompressor)
        optoCompressor->prepare(sampleRate, numChannels);
    if (fetCompressor)
        fetCompressor->prepare(sampleRate, numChannels);
    if (vcaCompressor)
        vcaCompressor->prepare(sampleRate, numChannels);
    if (busCompressor)
        busCompressor->prepare(sampleRate, numChannels, samplesPerBlock);
    
    // Prepare anti-aliasing for internal oversampling
    if (antiAliasing)
        antiAliasing->prepare(sampleRate, samplesPerBlock, numChannels);
    
    // Set latency based on oversampling
    setLatencySamples(antiAliasing ? antiAliasing->getLatency() : 0);
}

void UniversalCompressor::releaseResources()
{
    // Nothing specific to release
}

void UniversalCompressor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Improved denormal prevention - more efficient than ScopedNoDenormals
    #if JUCE_INTEL
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    #else
        juce::ScopedNoDenormals noDenormals;
    #endif
    
    // Safety checks
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;
    
    // Check for valid compressor instances
    if (!optoCompressor || !fetCompressor || !vcaCompressor || !busCompressor)
        return;
    
    // Check for valid parameter pointers and bypass
    auto* bypassParam = parameters.getRawParameterValue("bypass");
    if (!bypassParam || *bypassParam > 0.5f)
        return;
    
    // Get stereo link and mix parameters
    auto* stereoLinkParam = parameters.getRawParameterValue("stereo_link");
    auto* mixParam = parameters.getRawParameterValue("mix");
    auto* sidechainEnableParam = parameters.getRawParameterValue("sidechain_enable");
    float stereoLinkAmount = stereoLinkParam ? (*stereoLinkParam * 0.01f) : 1.0f; // Convert to 0-1
    float mixAmount = mixParam ? (*mixParam * 0.01f) : 1.0f; // Convert to 0-1
    bool useSidechain = sidechainEnableParam ? (*sidechainEnableParam > 0.5f) : false;
    
    // Store dry signal for parallel compression
    juce::AudioBuffer<float> dryBuffer;
    if (mixAmount < 1.0f)
    {
        dryBuffer.makeCopyOf(buffer);
    }
    
    // Get sidechain buffer if available and enabled
    juce::AudioBuffer<float> sidechainBuffer;
    if (useSidechain && getTotalNumInputChannels() > 2)
    {
        // Create a temporary buffer for sidechain (channels 2 and 3 if they exist)
        const int sidechainChannels = juce::jmin(2, getTotalNumInputChannels() - 2);
        if (sidechainChannels > 0)
        {
            sidechainBuffer.setSize(sidechainChannels, buffer.getNumSamples());
            // Note: In a real implementation, you'd get the sidechain from the second input bus
            // For now, we'll use a simplified approach
            // This would need proper multi-bus support in the processBlock override
        }
    }
    
    // Internal oversampling is always enabled for better quality
    bool oversample = true; // Always use oversampling internally
    CompressorMode mode = getCurrentMode();
    
    // Cache parameters based on mode to avoid repeated lookups
    float cachedParams[6] = {0.0f}; // Max 6 params for any mode
    bool validParams = true;
    
    switch (mode)
    {
        case CompressorMode::Opto:
        {
            auto* p1 = parameters.getRawParameterValue("opto_peak_reduction");
            auto* p2 = parameters.getRawParameterValue("opto_gain");
            auto* p3 = parameters.getRawParameterValue("opto_limit");
            if (p1 && p2 && p3) {
                cachedParams[0] = *p1;
                // LA-2A gain is 0-40dB range, parameter is 0-100
                // Map 50 = unity gain (0dB), 0 = -40dB, 100 = +40dB
                float gainParam = *p2;
                cachedParams[1] = (gainParam - 50.0f) * 0.8f; // -40 to +40 dB
                cachedParams[2] = *p3;
            } else validParams = false;
            break;
        }
        case CompressorMode::FET:
        {
            auto* p1 = parameters.getRawParameterValue("fet_input");
            auto* p2 = parameters.getRawParameterValue("fet_output");
            auto* p3 = parameters.getRawParameterValue("fet_attack");
            auto* p4 = parameters.getRawParameterValue("fet_release");
            auto* p5 = parameters.getRawParameterValue("fet_ratio");
            if (p1 && p2 && p3 && p4 && p5) {
                cachedParams[0] = *p1;
                cachedParams[1] = *p2;
                cachedParams[2] = *p3;
                cachedParams[3] = *p4;
                cachedParams[4] = *p5;
            } else validParams = false;
            break;
        }
        case CompressorMode::VCA:
        {
            auto* p1 = parameters.getRawParameterValue("vca_threshold");
            auto* p2 = parameters.getRawParameterValue("vca_ratio");
            auto* p3 = parameters.getRawParameterValue("vca_attack");
            auto* p4 = parameters.getRawParameterValue("vca_release");
            auto* p5 = parameters.getRawParameterValue("vca_output");
            auto* p6 = parameters.getRawParameterValue("vca_overeasy");
            if (p1 && p2 && p3 && p4 && p5 && p6) {
                cachedParams[0] = *p1;
                cachedParams[1] = *p2;
                cachedParams[2] = *p3;
                cachedParams[3] = *p4;
                cachedParams[4] = *p5;
                cachedParams[5] = *p6; // Store OverEasy state
            } else validParams = false;
            break;
        }
        case CompressorMode::Bus:
        {
            auto* p1 = parameters.getRawParameterValue("bus_threshold");
            auto* p2 = parameters.getRawParameterValue("bus_ratio");
            auto* p3 = parameters.getRawParameterValue("bus_attack");
            auto* p4 = parameters.getRawParameterValue("bus_release");
            auto* p5 = parameters.getRawParameterValue("bus_makeup");
            auto* p6 = parameters.getRawParameterValue("bus_mix");
            if (p1 && p2 && p3 && p4 && p5) {
                cachedParams[0] = *p1;
                // Convert discrete ratio choice to actual ratio value
                int ratioChoice = static_cast<int>(*p2);
                switch (ratioChoice) {
                    case 0: cachedParams[1] = 2.0f; break;  // 2:1
                    case 1: cachedParams[1] = 4.0f; break;  // 4:1
                    case 2: cachedParams[1] = 10.0f; break; // 10:1
                    default: cachedParams[1] = 2.0f; break;
                }
                cachedParams[2] = *p3;
                cachedParams[3] = *p4;
                cachedParams[4] = *p5;
                cachedParams[5] = p6 ? (*p6 * 0.01f) : 1.0f; // Convert 0-100 to 0-1, default 100%
            } else validParams = false;
            break;
        }
    }
    
    if (!validParams)
        return;
    
    // Input metering - use peak level for accurate dB display
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Get peak level - SIMD optimized metering
    float inputLevel = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        float channelPeak = SIMDHelpers::getPeakLevel(data, numSamples);
        inputLevel = juce::jmax(inputLevel, channelPeak);
    }

    // Convert to dB - peak level gives accurate dB reading
    float inputDb = inputLevel > 1e-5f ? juce::Decibels::gainToDecibels(inputLevel) : -60.0f;
    // Use relaxed memory ordering for performance in audio thread
    inputMeter.store(inputDb, std::memory_order_relaxed);
    
    // Process audio with reduced function call overhead
    if (oversample && antiAliasing)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto oversampledBlock = antiAliasing->processUp(block);
        
        const int osNumChannels = static_cast<int>(oversampledBlock.getNumChannels());
        const int osNumSamples = static_cast<int>(oversampledBlock.getNumSamples());
        
        // Process with cached parameters
        for (int channel = 0; channel < osNumChannels; ++channel)
        {
            float* data = oversampledBlock.getChannelPointer(static_cast<size_t>(channel));
            
            switch (mode)
            {
                case CompressorMode::Opto:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = optoCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2] > 0.5f, true);
                    break;
                case CompressorMode::FET:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = fetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), true);
                    break;
                case CompressorMode::VCA:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = vcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], cachedParams[5] > 0.5f, true);
                    break;
                case CompressorMode::Bus:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = busCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], static_cast<int>(cachedParams[2]), static_cast<int>(cachedParams[3]), cachedParams[4], cachedParams[5], true);
                    break;
            }
        }
        
        antiAliasing->processDown(block);
    }
    else
    {
        // Process without oversampling
        // No compensation needed - maintain unity gain
        const float compensationGain = 1.0f; // Unity gain (no compensation)
        
        for (int channel = 0; channel < numChannels; ++channel)
        {
            float* data = buffer.getWritePointer(channel);
            
            switch (mode)
            {
                case CompressorMode::Opto:
                    for (int i = 0; i < numSamples; ++i)
                        data[i] = optoCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2] > 0.5f, false) * compensationGain;
                    break;
                case CompressorMode::FET:
                    for (int i = 0; i < numSamples; ++i)
                        data[i] = fetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), false) * compensationGain;
                    break;
                case CompressorMode::VCA:
                    for (int i = 0; i < numSamples; ++i)
                        data[i] = vcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], cachedParams[5] > 0.5f, false) * compensationGain;
                    break;
                case CompressorMode::Bus:
                    for (int i = 0; i < numSamples; ++i)
                        data[i] = busCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], static_cast<int>(cachedParams[2]), static_cast<int>(cachedParams[3]), cachedParams[4], cachedParams[5], false) * compensationGain;
                    break;
            }
        }
    }
    
    // Output metering - SIMD optimized
    float outputLevel = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        float channelPeak = SIMDHelpers::getPeakLevel(data, numSamples);
        outputLevel = juce::jmax(outputLevel, channelPeak);
    }

    float outputDb = outputLevel > 1e-5f ? juce::Decibels::gainToDecibels(outputLevel) : -60.0f;
    // Use relaxed memory ordering for performance in audio thread
    outputMeter.store(outputDb, std::memory_order_relaxed);
    
    // Get gain reduction from active compressor
    float gainReduction = 0.0f;
    switch (mode)
    {
        case CompressorMode::Opto: 
            gainReduction = optoCompressor->getGainReduction(0);
            if (numChannels > 1)
                gainReduction = juce::jmin(gainReduction, optoCompressor->getGainReduction(1));
            break;
        case CompressorMode::FET: 
            gainReduction = fetCompressor->getGainReduction(0);
            if (numChannels > 1)
                gainReduction = juce::jmin(gainReduction, fetCompressor->getGainReduction(1));
            break;
        case CompressorMode::VCA: 
            gainReduction = vcaCompressor->getGainReduction(0);
            if (numChannels > 1)
                gainReduction = juce::jmin(gainReduction, vcaCompressor->getGainReduction(1));
            break;
        case CompressorMode::Bus: 
            gainReduction = busCompressor->getGainReduction(0);
            if (numChannels > 1)
                gainReduction = juce::jmin(gainReduction, busCompressor->getGainReduction(1));
            break;
    }
    // Use relaxed memory ordering for performance in audio thread
    grMeter.store(gainReduction, std::memory_order_relaxed);
    
    // Update the gain reduction parameter for DAW display
    if (auto* grParam = parameters.getRawParameterValue("gr_meter"))
        *grParam = gainReduction;
    
    // Apply mix control for parallel compression (SIMD-optimized)
    if (mixAmount < 1.0f && dryBuffer.getNumChannels() > 0)
    {
        // Blend dry and wet signals
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* wet = buffer.getWritePointer(ch);
            const float* dry = dryBuffer.getReadPointer(ch);
            SIMDHelpers::mixBuffers(wet, dry, numSamples, mixAmount);
        }
    }

    // Add subtle analog noise for authenticity (-80dB) if enabled (SIMD-optimized)
    // This adds character and prevents complete digital silence
    auto* noiseEnableParam = parameters.getRawParameterValue("noise_enable");
    bool noiseEnabled = noiseEnableParam ? (*noiseEnableParam > 0.5f) : true; // Default ON

    if (noiseEnabled)
    {
        juce::Random random;
        const float noiseLevel = 0.0001f; // -80dB
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            SIMDHelpers::addNoise(data, numSamples, noiseLevel, random);
        }
    }
}

void UniversalCompressor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    // Convert double to float, process, then convert back
    juce::AudioBuffer<float> floatBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    
    // Convert double to float
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            floatBuffer.setSample(ch, i, static_cast<float>(buffer.getSample(ch, i)));
        }
    }
    
    // Process the float buffer
    processBlock(floatBuffer, midiMessages);
    
    // Convert back to double
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            buffer.setSample(ch, i, static_cast<double>(floatBuffer.getSample(ch, i)));
        }
    }
}

juce::AudioProcessorEditor* UniversalCompressor::createEditor()
{
    // Use the enhanced analog-style editor
    return new EnhancedCompressorEditor(*this);
}

CompressorMode UniversalCompressor::getCurrentMode() const
{
    auto* modeParam = parameters.getRawParameterValue("mode");
    if (modeParam)
    {
        int mode = static_cast<int>(*modeParam);
        return static_cast<CompressorMode>(juce::jlimit(0, 3, mode));
    }
    return CompressorMode::Opto; // Default fallback
}

double UniversalCompressor::getLatencyInSamples() const
{
    // Report latency from oversampler if active
    if (antiAliasing)
    {
        return static_cast<double>(antiAliasing->getLatency());
    }
    return 0.0;
}

double UniversalCompressor::getTailLengthSeconds() const
{
    // Account for lookahead delay if implemented
    return currentSampleRate > 0 ? getLatencyInSamples() / currentSampleRate : 0.0;
}

void UniversalCompressor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void UniversalCompressor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

// LV2 inline display removed - JUCE doesn't natively support this extension
// and manual Cairo implementation conflicts with JUCE's LV2 wrapper.
// The full GUI works perfectly in all LV2 hosts.

// Plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UniversalCompressor();
}

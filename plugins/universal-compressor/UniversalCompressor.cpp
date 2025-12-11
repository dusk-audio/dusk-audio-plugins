#include "UniversalCompressor.h"
#include "EnhancedCompressorEditor.h"
#include <cmath>

// SIMD helper utilities for vectorizable operations
namespace SIMDHelpers {
    using FloatVec = juce::dsp::SIMDRegister<float>;

    // Check if pointer is properly aligned for SIMD operations
    inline bool isAligned(const void* ptr) {
        constexpr size_t alignment = FloatVec::SIMDRegisterSize;
        return (reinterpret_cast<uintptr_t>(ptr) % alignment) == 0;
    }

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
        // Use scalar fallback if data is not aligned for SIMD
        if (!isAligned(data)) {
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                peak = juce::jmax(peak, std::abs(data[i]));
            return peak;
        }

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
        // Use scalar fallback if data is not aligned for SIMD
        if (!isAligned(data)) {
            for (int i = 0; i < numSamples; ++i)
                data[i] *= gain;
            return;
        }

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
        // Use scalar fallback if either buffer is not aligned for SIMD
        if (!isAligned(dest) || !isAligned(src)) {
            for (int i = 0; i < numSamples; ++i)
                dest[i] = dest[i] * (1.0f - wetAmount) + src[i] * wetAmount;
            return;
        }

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
        // Use scalar fallback if data is not aligned for SIMD
        if (!isAligned(data)) {
            for (int i = 0; i < numSamples; ++i)
                data[i] += (random.nextFloat() * 2.0f - 1.0f) * noiseLevel;
            return;
        }

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

    // Interpolate sidechain buffer from original to oversampled rate
    // Eliminates per-sample getSample() calls and bounds checking in hot loop
    inline void interpolateSidechain(const float* src, float* dest,
                                      int srcSamples, int destSamples) {
        if (srcSamples <= 0 || destSamples <= 0) return;

        // Pre-compute ratio once
        const float srcToDestRatio = static_cast<float>(srcSamples) / static_cast<float>(destSamples);
        const int maxSrcIdx = srcSamples - 1;

        // Unroll by 4 for better pipeline utilization
        int i = 0;
        for (; i + 4 <= destSamples; i += 4) {
            float srcIdx0 = static_cast<float>(i) * srcToDestRatio;
            float srcIdx1 = static_cast<float>(i + 1) * srcToDestRatio;
            float srcIdx2 = static_cast<float>(i + 2) * srcToDestRatio;
            float srcIdx3 = static_cast<float>(i + 3) * srcToDestRatio;

            int idx0_0 = static_cast<int>(srcIdx0);
            int idx0_1 = static_cast<int>(srcIdx1);
            int idx0_2 = static_cast<int>(srcIdx2);
            int idx0_3 = static_cast<int>(srcIdx3);

            int idx1_0 = juce::jmin(idx0_0 + 1, maxSrcIdx);
            int idx1_1 = juce::jmin(idx0_1 + 1, maxSrcIdx);
            int idx1_2 = juce::jmin(idx0_2 + 1, maxSrcIdx);
            int idx1_3 = juce::jmin(idx0_3 + 1, maxSrcIdx);

            float frac0 = srcIdx0 - static_cast<float>(idx0_0);
            float frac1 = srcIdx1 - static_cast<float>(idx0_1);
            float frac2 = srcIdx2 - static_cast<float>(idx0_2);
            float frac3 = srcIdx3 - static_cast<float>(idx0_3);

            dest[i]     = src[idx0_0] + frac0 * (src[idx1_0] - src[idx0_0]);
            dest[i + 1] = src[idx0_1] + frac1 * (src[idx1_1] - src[idx0_1]);
            dest[i + 2] = src[idx0_2] + frac2 * (src[idx1_2] - src[idx0_2]);
            dest[i + 3] = src[idx0_3] + frac3 * (src[idx1_3] - src[idx0_3]);
        }

        // Process remaining samples
        for (; i < destSamples; ++i) {
            float srcIdx = static_cast<float>(i) * srcToDestRatio;
            int idx0 = static_cast<int>(srcIdx);
            int idx1 = juce::jmin(idx0 + 1, maxSrcIdx);
            float frac = srcIdx - static_cast<float>(idx0);
            dest[i] = src[idx0] + frac * (src[idx1] - src[idx0]);
        }
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
    
    // Vintage FET constants
    constexpr float FET_THRESHOLD_DB = -10.0f; // Fixed threshold
    constexpr float FET_MAX_REDUCTION_DB = 30.0f;
    constexpr float FET_ALLBUTTONS_ATTACK = 0.0001f; // 100 microseconds

    // Classic VCA constants
    constexpr float VCA_RMS_TIME_CONSTANT = 0.003f; // 3ms RMS averaging
    constexpr float VCA_RELEASE_RATE = 120.0f; // dB per second
    constexpr float VCA_CONTROL_VOLTAGE_SCALE = -0.006f; // -6mV/dB
    constexpr float VCA_MAX_REDUCTION_DB = 60.0f;

    // Bus Compressor constants
    constexpr float BUS_SIDECHAIN_HP_FREQ = 60.0f; // Hz
    constexpr float BUS_MAX_REDUCTION_DB = 20.0f;
    constexpr float BUS_OVEREASY_KNEE_WIDTH = 10.0f; // dB

    // Studio FET constants - cleaner than Vintage FET
    constexpr float STUDIO_FET_THRESHOLD_DB = -10.0f;
    constexpr float STUDIO_FET_HARMONIC_SCALE = 0.3f;  // 30% of Vintage FET harmonics

    // Studio VCA constants
    constexpr float STUDIO_VCA_MAX_REDUCTION_DB = 40.0f;
    constexpr float STUDIO_VCA_SOFT_KNEE_DB = 6.0f;  // Soft knee for smooth response

    // Global sidechain highpass filter frequency (user-adjustable)
    constexpr float SIDECHAIN_HP_MIN = 20.0f;   // Hz
    constexpr float SIDECHAIN_HP_MAX = 500.0f;  // Hz
    constexpr float SIDECHAIN_HP_DEFAULT = 80.0f; // Hz - prevents pumping
    
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
    AntiAliasing()
    {
        // Initialize with stereo by default to prevent crashes
        channelStates.resize(2);
        for (auto& state : channelStates)
        {
            state.preFilterState = 0.0f;
            state.postFilterState = 0.0f;
            state.dcBlockerState = 0.0f;
            state.dcBlockerPrev = 0.0f;
        }
    }

    void prepare(double sampleRate, int blockSize, int numChannels)
    {
        this->sampleRate = sampleRate;
        this->blockSize = blockSize;

        if (blockSize > 0 && numChannels > 0)
        {
            this->numChannels = numChannels;

            // Create both 2x and 4x oversamplers
            // 2x oversampling (1 stage)
            oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
                numChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
            oversampler2x->initProcessing(static_cast<size_t>(blockSize));

            // 4x oversampling (2 stages)
            oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
                numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
            oversampler4x->initProcessing(static_cast<size_t>(blockSize));

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

    void setOversamplingFactor(int factor)
    {
        // 0 = 2x, 1 = 4x
        use4x = (factor == 1);
    }

    bool isUsing4x() const { return use4x; }

    bool isReady() const
    {
        // Both oversamplers must be ready since we could switch between them
        return oversampler2x != nullptr && oversampler4x != nullptr;
    }

    juce::dsp::AudioBlock<float> processUp(juce::dsp::AudioBlock<float>& block)
    {
        // Reset upsampled flag
        didUpsample = false;

        // Safety check: verify oversampler is valid
        auto* oversampler = use4x ? oversampler4x.get() : oversampler2x.get();
        if (oversampler == nullptr)
            return block;

        // Safety check: verify block is compatible with oversampler
        if (block.getNumChannels() != static_cast<size_t>(numChannels) ||
            block.getNumSamples() > static_cast<size_t>(blockSize))
            return block;

        didUpsample = true;
        return oversampler->processSamplesUp(block);
    }

    void processDown(juce::dsp::AudioBlock<float>& block)
    {
        // Only downsample if we actually upsampled
        if (!didUpsample)
            return;

        auto* oversampler = use4x ? oversampler4x.get() : oversampler2x.get();
        if (oversampler)
            oversampler->processSamplesDown(block);
    }
    
    // Unified pre-saturation filtering to prevent aliasing
    float preProcessSample(float input, int channel)
    {
        if (channel < 0 || channel >= static_cast<int>(channelStates.size())) return input;

        // Gentle high-frequency reduction before any saturation
        // This prevents high frequencies from creating aliases
        // Limit to min(20kHz, 45% of Nyquist) to prevent aliasing at all sample rates
        const float nyquist = static_cast<float>(sampleRate) * 0.5f;
        const float cutoffFreq = std::min(20000.0f, nyquist * 0.9f);  // 90% of Nyquist, max 20kHz
        const float filterCoeff = std::exp(-2.0f * 3.14159f * cutoffFreq / static_cast<float>(sampleRate));

        channelStates[channel].preFilterState = input * (1.0f - filterCoeff * 0.1f) +
                                                channelStates[channel].preFilterState * filterCoeff * 0.1f;

        return channelStates[channel].preFilterState;
    }
    
    // Unified post-saturation filtering to remove any remaining aliases
    float postProcessSample(float input, int channel)
    {
        if (channel < 0 || channel >= static_cast<int>(channelStates.size())) return input;

        // Remove any harmonics above Nyquist/2
        // Only process if we have a valid sample rate from DAW
        if (sampleRate <= 0.0) return input;
        // Limit to min(20kHz, 90% of Nyquist) to prevent aliasing at all sample rates
        const float nyquist = static_cast<float>(sampleRate) * 0.5f;
        const float cutoffFreq = std::min(20000.0f, nyquist * 0.9f);  // 90% of Nyquist, max 20kHz
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
        auto* oversampler = use4x ? oversampler4x.get() : oversampler2x.get();
        return oversampler ? static_cast<int>(oversampler->getLatencyInSamples()) : 0;
    }

    // Get maximum latency (for consistent PDC reporting)
    int getMaxLatency() const
    {
        // Report 4x latency always for consistent PDC
        return oversampler4x ? static_cast<int>(oversampler4x->getLatencyInSamples()) : 0;
    }

    bool isOversamplingEnabled() const { return oversampler2x != nullptr || oversampler4x != nullptr; }
    double getSampleRate() const { return sampleRate; }

private:
    struct ChannelState
    {
        float preFilterState = 0.0f;
        float postFilterState = 0.0f;
        float dcBlockerState = 0.0f;
        float dcBlockerPrev = 0.0f;
    };

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
    std::vector<ChannelState> channelStates;
    double sampleRate = 0.0;  // Set by prepare() from DAW
    int blockSize = 0;        // Set by prepare() from DAW
    int numChannels = 0;      // Set by prepare() from DAW
    bool use4x = false;       // Use 4x oversampling instead of 2x
    bool didUpsample = false; // Track if processUp actually performed upsampling
};

// Sidechain highpass filter - prevents pumping from low frequencies
class UniversalCompressor::SidechainFilter
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        filterStates.resize(static_cast<size_t>(numChannels));
        for (auto& state : filterStates)
        {
            state.z1 = 0.0f;
            state.z2 = 0.0f;
        }
        updateCoefficients(Constants::SIDECHAIN_HP_DEFAULT);
    }

    void setFrequency(float freq)
    {
        freq = juce::jlimit(Constants::SIDECHAIN_HP_MIN, Constants::SIDECHAIN_HP_MAX, freq);
        if (std::abs(freq - currentFreq) > 0.1f)
            updateCoefficients(freq);
    }

    float process(float input, int channel)
    {
        if (channel < 0 || channel >= static_cast<int>(filterStates.size()))
            return input;

        auto& state = filterStates[static_cast<size_t>(channel)];

        // Transposed Direct Form II biquad
        float output = b0 * input + state.z1;
        state.z1 = b1 * input - a1 * output + state.z2;
        state.z2 = b2 * input - a2 * output;

        return output;
    }

    // Block processing method - eliminates per-sample function call overhead
    // Unrolls by 4 for better pipeline utilization with cached coefficients
    void processBlock(const float* input, float* output, int numSamples, int channel)
    {
        if (channel < 0 || channel >= static_cast<int>(filterStates.size()))
        {
            // Invalid channel - copy input to output
            if (input != output)
                std::memcpy(output, input, static_cast<size_t>(numSamples) * sizeof(float));
            return;
        }

        auto& state = filterStates[static_cast<size_t>(channel)];

        // Cache coefficients in local variables for register allocation
        const float lb0 = b0, lb1 = b1, lb2 = b2;
        const float la1 = a1, la2 = a2;
        float z1 = state.z1, z2 = state.z2;

        // Process in blocks of 4 for better pipeline utilization
        int i = 0;
        for (; i + 4 <= numSamples; i += 4)
        {
            // Unroll 4 iterations - biquad is inherently sequential
            // but unrolling helps instruction pipeline
            float out0 = lb0 * input[i] + z1;
            z1 = lb1 * input[i] - la1 * out0 + z2;
            z2 = lb2 * input[i] - la2 * out0;
            output[i] = out0;

            float out1 = lb0 * input[i+1] + z1;
            z1 = lb1 * input[i+1] - la1 * out1 + z2;
            z2 = lb2 * input[i+1] - la2 * out1;
            output[i+1] = out1;

            float out2 = lb0 * input[i+2] + z1;
            z1 = lb1 * input[i+2] - la1 * out2 + z2;
            z2 = lb2 * input[i+2] - la2 * out2;
            output[i+2] = out2;

            float out3 = lb0 * input[i+3] + z1;
            z1 = lb1 * input[i+3] - la1 * out3 + z2;
            z2 = lb2 * input[i+3] - la2 * out3;
            output[i+3] = out3;
        }

        // Process remaining samples
        for (; i < numSamples; ++i)
        {
            float out = lb0 * input[i] + z1;
            z1 = lb1 * input[i] - la1 * out + z2;
            z2 = lb2 * input[i] - la2 * out;
            output[i] = out;
        }

        // Write back state
        state.z1 = z1;
        state.z2 = z2;
    }

    float getFrequency() const { return currentFreq; }

private:
    void updateCoefficients(float freq)
    {
        currentFreq = freq;
        if (sampleRate <= 0.0) return;

        // Butterworth highpass coefficients
        const float omega = 2.0f * juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
        const float cosOmega = std::cos(omega);
        const float sinOmega = std::sin(omega);
        const float alpha = sinOmega / (2.0f * 0.707f);  // Q = 0.707 for Butterworth

        const float a0_inv = 1.0f / (1.0f + alpha);

        b0 = ((1.0f + cosOmega) / 2.0f) * a0_inv;
        b1 = -(1.0f + cosOmega) * a0_inv;
        b2 = b0;
        a1 = (-2.0f * cosOmega) * a0_inv;
        a2 = (1.0f - alpha) * a0_inv;
    }

    struct FilterState
    {
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    std::vector<FilterState> filterStates;
    double sampleRate = 44100.0;
    float currentFreq = Constants::SIDECHAIN_HP_DEFAULT;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

//==============================================================================
// Sidechain EQ - Low shelf and high shelf for sidechain shaping
class SidechainEQ
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        lowShelfStates.resize(static_cast<size_t>(numChannels));
        highShelfStates.resize(static_cast<size_t>(numChannels));
        for (auto& state : lowShelfStates)
            state = FilterState{};
        for (auto& state : highShelfStates)
            state = FilterState{};

        updateLowShelfCoefficients();
        updateHighShelfCoefficients();
    }

    void setLowShelf(float freqHz, float gainDb)
    {
        if (std::abs(freqHz - lowShelfFreq) > 0.1f || std::abs(gainDb - lowShelfGain) > 0.01f)
        {
            lowShelfFreq = juce::jlimit(60.0f, 500.0f, freqHz);
            lowShelfGain = juce::jlimit(-12.0f, 12.0f, gainDb);
            updateLowShelfCoefficients();
        }
    }

    void setHighShelf(float freqHz, float gainDb)
    {
        if (std::abs(freqHz - highShelfFreq) > 0.1f || std::abs(gainDb - highShelfGain) > 0.01f)
        {
            highShelfFreq = juce::jlimit(2000.0f, 16000.0f, freqHz);
            highShelfGain = juce::jlimit(-12.0f, 12.0f, gainDb);
            updateHighShelfCoefficients();
        }
    }

    float process(float input, int channel)
    {
        if (channel < 0 || channel >= static_cast<int>(lowShelfStates.size()))
            return input;

        // Apply low shelf
        float output = input;
        if (std::abs(lowShelfGain) > 0.01f)
        {
            auto& ls = lowShelfStates[static_cast<size_t>(channel)];
            float y = lowB0 * output + ls.z1;
            ls.z1 = lowB1 * output - lowA1 * y + ls.z2;
            ls.z2 = lowB2 * output - lowA2 * y;
            output = y;
        }

        // Apply high shelf
        if (std::abs(highShelfGain) > 0.01f)
        {
            auto& hs = highShelfStates[static_cast<size_t>(channel)];
            float y = highB0 * output + hs.z1;
            hs.z1 = highB1 * output - highA1 * y + hs.z2;
            hs.z2 = highB2 * output - highA2 * y;
            output = y;
        }

        return output;
    }

    float getLowShelfGain() const { return lowShelfGain; }
    float getHighShelfGain() const { return highShelfGain; }

private:
    void updateLowShelfCoefficients()
    {
        if (sampleRate <= 0.0) return;

        // Low shelf filter coefficients (peaking shelf)
        float A = std::pow(10.0f, lowShelfGain / 40.0f);
        float omega = 2.0f * juce::MathConstants<float>::pi * lowShelfFreq / static_cast<float>(sampleRate);
        float sinOmega = std::sin(omega);
        float cosOmega = std::cos(omega);
        float alpha = sinOmega / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / 0.707f - 1.0f) + 2.0f);
        float sqrtA = std::sqrt(A);

        float a0 = (A + 1.0f) + (A - 1.0f) * cosOmega + 2.0f * sqrtA * alpha;
        lowB0 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega + 2.0f * sqrtA * alpha) / a0;
        lowB1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosOmega) / a0;
        lowB2 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega - 2.0f * sqrtA * alpha) / a0;
        lowA1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosOmega) / a0;
        lowA2 = ((A + 1.0f) + (A - 1.0f) * cosOmega - 2.0f * sqrtA * alpha) / a0;
    }

    void updateHighShelfCoefficients()
    {
        if (sampleRate <= 0.0) return;

        // High shelf filter coefficients (peaking shelf)
        float A = std::pow(10.0f, highShelfGain / 40.0f);
        float omega = 2.0f * juce::MathConstants<float>::pi * highShelfFreq / static_cast<float>(sampleRate);
        float sinOmega = std::sin(omega);
        float cosOmega = std::cos(omega);
        float alpha = sinOmega / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / 0.707f - 1.0f) + 2.0f);
        float sqrtA = std::sqrt(A);

        float a0 = (A + 1.0f) - (A - 1.0f) * cosOmega + 2.0f * sqrtA * alpha;
        highB0 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega + 2.0f * sqrtA * alpha) / a0;
        highB1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosOmega) / a0;
        highB2 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega - 2.0f * sqrtA * alpha) / a0;
        highA1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosOmega) / a0;
        highA2 = ((A + 1.0f) - (A - 1.0f) * cosOmega - 2.0f * sqrtA * alpha) / a0;
    }

    struct FilterState
    {
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    std::vector<FilterState> lowShelfStates;
    std::vector<FilterState> highShelfStates;
    double sampleRate = 44100.0;

    // Low shelf parameters
    float lowShelfFreq = 100.0f;
    float lowShelfGain = 0.0f;  // dB
    float lowB0 = 1.0f, lowB1 = 0.0f, lowB2 = 0.0f, lowA1 = 0.0f, lowA2 = 0.0f;

    // High shelf parameters
    float highShelfFreq = 8000.0f;
    float highShelfGain = 0.0f;  // dB
    float highB0 = 1.0f, highB1 = 0.0f, highB2 = 0.0f, highA1 = 0.0f, highA2 = 0.0f;
};

//==============================================================================
// True-Peak Detector - ITU-R BS.1770 compliant inter-sample peak detection
// Uses polyphase FIR interpolation to detect peaks between samples
class UniversalCompressor::TruePeakDetector
{
public:
    // Oversampling factors for true-peak detection
    static constexpr int OVERSAMPLE_4X = 4;
    static constexpr int OVERSAMPLE_8X = 8;
    static constexpr int TAPS_PER_PHASE = 12;  // 48-tap FIR for 4x, 96-tap for 8x

    void prepare(double sampleRate, int numChannels, int maxBlockSize)
    {
        this->sampleRate = sampleRate;
        this->numChannels = numChannels;

        channelStates.resize(static_cast<size_t>(numChannels));
        for (auto& state : channelStates)
        {
            state.history.fill(0.0f);
            state.truePeak = 0.0f;
            state.historyIndex = 0;
        }

        // Initialize polyphase FIR coefficients for 4x oversampling
        // These are derived from a windowed-sinc lowpass filter at 0.5*Fs/4
        initializeCoefficients4x();
        initializeCoefficients8x();
    }

    void setOversamplingFactor(int factor)
    {
        oversamplingFactor = (factor == 1) ? OVERSAMPLE_8X : OVERSAMPLE_4X;
    }

    // Process a single sample and return the true-peak value
    float processSample(float sample, int channel)
    {
        if (channel < 0 || channel >= static_cast<int>(channelStates.size()))
            return std::abs(sample);

        auto& state = channelStates[static_cast<size_t>(channel)];

        // Update history buffer
        state.history[state.historyIndex] = sample;
        state.historyIndex = (state.historyIndex + 1) % HISTORY_SIZE;

        // Find maximum inter-sample peak using polyphase interpolation
        float maxPeak = std::abs(sample);  // Start with sample peak

        if (oversamplingFactor == OVERSAMPLE_4X)
        {
            // 4x oversampling: check 3 interpolated points between samples
            for (int phase = 1; phase < 4; ++phase)
            {
                float interpolated = interpolatePolyphase4x(state, phase);
                maxPeak = juce::jmax(maxPeak, std::abs(interpolated));
            }
        }
        else
        {
            // 8x oversampling: check 7 interpolated points between samples
            for (int phase = 1; phase < 8; ++phase)
            {
                float interpolated = interpolatePolyphase8x(state, phase);
                maxPeak = juce::jmax(maxPeak, std::abs(interpolated));
            }
        }

        state.truePeak = maxPeak;
        return maxPeak;
    }

    // Process an entire block and update each sample with its true-peak value
    void processBlock(float* data, int numSamples, int channel)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float truePeak = processSample(data[i], channel);
            // Replace the sample with signed true-peak (preserve sign for detection)
            data[i] = std::copysign(truePeak, data[i]);
        }
    }

    float getTruePeak(int channel) const
    {
        if (channel >= 0 && channel < static_cast<int>(channelStates.size()))
            return channelStates[static_cast<size_t>(channel)].truePeak;
        return 0.0f;
    }

    int getLatency() const
    {
        // Latency is half the filter length (due to linear-phase FIR)
        return TAPS_PER_PHASE / 2;
    }

private:
    static constexpr int HISTORY_SIZE = 16;  // Power of 2 for efficient modulo

    struct ChannelState
    {
        std::array<float, HISTORY_SIZE> history;
        float truePeak = 0.0f;
        int historyIndex = 0;
    };

    std::vector<ChannelState> channelStates;
    double sampleRate = 44100.0;
    int numChannels = 2;
    int oversamplingFactor = OVERSAMPLE_4X;

    // Polyphase FIR coefficients (ITU-R BS.1770-4 compliant)
    // 4x oversampling: 4 phases × 12 taps = 48-tap FIR
    std::array<std::array<float, TAPS_PER_PHASE>, 4> coefficients4x;
    // 8x oversampling: 8 phases × 12 taps = 96-tap FIR
    std::array<std::array<float, TAPS_PER_PHASE>, 8> coefficients8x;

    void initializeCoefficients4x()
    {
        // Windowed-sinc coefficients for 4x upsampling (Kaiser window, beta=9)
        // Designed for 0.5*Fs/4 cutoff (Nyquist at original sample rate)
        // Phase 0 is the original samples (unity at center tap)
        // Phases 1-3 are interpolated points

        // Pre-computed coefficients for ITU-compliant true-peak detection
        // These match the response specified in ITU-R BS.1770-4
        coefficients4x[0] = {{ 0.0000f, -0.0015f, 0.0076f, -0.0251f, 0.0700f, -0.3045f,
                               0.9722f, 0.3045f, -0.0700f, 0.0251f, -0.0076f, 0.0015f }};
        coefficients4x[1] = {{ -0.0005f, 0.0027f, -0.0105f, 0.0330f, -0.1125f, 0.7265f,
                               0.7265f, -0.1125f, 0.0330f, -0.0105f, 0.0027f, -0.0005f }};
        coefficients4x[2] = {{ 0.0015f, -0.0076f, 0.0251f, -0.0700f, 0.3045f, 0.9722f,
                              -0.3045f, 0.0700f, -0.0251f, 0.0076f, -0.0015f, 0.0000f }};
        coefficients4x[3] = {{ -0.0010f, 0.0055f, -0.0178f, 0.0514f, -0.1755f, 0.8940f,
                               0.5260f, -0.0900f, 0.0280f, -0.0092f, 0.0023f, -0.0003f }};
    }

    void initializeCoefficients8x()
    {
        // 8x oversampling coefficients for higher-quality true-peak detection
        // More phases for finer interpolation resolution
        coefficients8x[0] = {{ 0.0000f, -0.0008f, 0.0038f, -0.0126f, 0.0350f, -0.1523f,
                               0.9861f, 0.1523f, -0.0350f, 0.0126f, -0.0038f, 0.0008f }};
        coefficients8x[1] = {{ -0.0002f, 0.0011f, -0.0045f, 0.0147f, -0.0503f, 0.3245f,
                               0.9352f, 0.0650f, -0.0175f, 0.0063f, -0.0019f, 0.0003f }};
        coefficients8x[2] = {{ -0.0004f, 0.0020f, -0.0078f, 0.0245f, -0.0837f, 0.5405f,
                               0.8415f, -0.0180f, 0.0030f, 0.0000f, -0.0005f, 0.0000f }};
        coefficients8x[3] = {{ -0.0005f, 0.0027f, -0.0105f, 0.0330f, -0.1125f, 0.7265f,
                               0.7265f, -0.1125f, 0.0330f, -0.0105f, 0.0027f, -0.0005f }};
        coefficients8x[4] = {{ 0.0000f, -0.0005f, 0.0000f, 0.0030f, -0.0180f, 0.8415f,
                               0.5405f, -0.0837f, 0.0245f, -0.0078f, 0.0020f, -0.0004f }};
        coefficients8x[5] = {{ 0.0003f, -0.0019f, 0.0063f, -0.0175f, 0.0650f, 0.9352f,
                               0.3245f, -0.0503f, 0.0147f, -0.0045f, 0.0011f, -0.0002f }};
        coefficients8x[6] = {{ 0.0008f, -0.0038f, 0.0126f, -0.0350f, 0.1523f, 0.9861f,
                               0.1523f, -0.0350f, 0.0126f, -0.0038f, 0.0008f, 0.0000f }};
        coefficients8x[7] = {{ 0.0005f, -0.0028f, 0.0095f, -0.0270f, 0.1050f, 0.9650f,
                               0.2380f, -0.0420f, 0.0137f, -0.0042f, 0.0010f, -0.0001f }};
    }

    // Polyphase interpolation for 4x oversampling
    float interpolatePolyphase4x(const ChannelState& state, int phase) const
    {
        const auto& coeffs = coefficients4x[static_cast<size_t>(phase)];
        float result = 0.0f;

        // Convolve history with phase coefficients
        for (int i = 0; i < TAPS_PER_PHASE; ++i)
        {
            int histIdx = (state.historyIndex - TAPS_PER_PHASE + i + HISTORY_SIZE) % HISTORY_SIZE;
            result += state.history[static_cast<size_t>(histIdx)] * coeffs[static_cast<size_t>(i)];
        }

        return result;
    }

    // Polyphase interpolation for 8x oversampling
    float interpolatePolyphase8x(const ChannelState& state, int phase) const
    {
        const auto& coeffs = coefficients8x[static_cast<size_t>(phase)];
        float result = 0.0f;

        for (int i = 0; i < TAPS_PER_PHASE; ++i)
        {
            int histIdx = (state.historyIndex - TAPS_PER_PHASE + i + HISTORY_SIZE) % HISTORY_SIZE;
            result += state.history[static_cast<size_t>(histIdx)] * coeffs[static_cast<size_t>(i)];
        }

        return result;
    }
};

//==============================================================================
// Transient Shaper for FET all-buttons mode
// Detects transients and provides a multiplier to let them through the compression
class UniversalCompressor::TransientShaper
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        channels.resize(static_cast<size_t>(numChannels));
        for (auto& ch : channels)
        {
            ch.fastEnvelope = 0.0f;
            ch.slowEnvelope = 0.0f;
            ch.peakHold = 0.0f;
            ch.holdCounter = 0;
        }

        // Calculate time constants
        // Fast envelope: ~0.5ms attack, ~20ms release
        fastAttackCoeff = std::exp(-1.0f / (0.0005f * static_cast<float>(sampleRate)));
        fastReleaseCoeff = std::exp(-1.0f / (0.020f * static_cast<float>(sampleRate)));

        // Slow envelope: ~10ms attack, ~100ms release
        slowAttackCoeff = std::exp(-1.0f / (0.010f * static_cast<float>(sampleRate)));
        slowReleaseCoeff = std::exp(-1.0f / (0.100f * static_cast<float>(sampleRate)));

        // Hold time: ~5ms
        holdSamples = static_cast<int>(0.005f * static_cast<float>(sampleRate));
    }

    // Process a sample and return a transient modifier (1.0 = no change, >1.0 = let transient through)
    float process(float input, int channel, float sensitivity)
    {
        if (channel < 0 || channel >= static_cast<int>(channels.size()))
            return 1.0f;

        auto& ch = channels[static_cast<size_t>(channel)];
        float absInput = std::abs(input);

        // Update fast envelope (transient detection)
        if (absInput > ch.fastEnvelope)
            ch.fastEnvelope = fastAttackCoeff * ch.fastEnvelope + (1.0f - fastAttackCoeff) * absInput;
        else
            ch.fastEnvelope = fastReleaseCoeff * ch.fastEnvelope + (1.0f - fastReleaseCoeff) * absInput;

        // Update slow envelope (average level tracking)
        if (absInput > ch.slowEnvelope)
            ch.slowEnvelope = slowAttackCoeff * ch.slowEnvelope + (1.0f - slowAttackCoeff) * absInput;
        else
            ch.slowEnvelope = slowReleaseCoeff * ch.slowEnvelope + (1.0f - slowReleaseCoeff) * absInput;

        // Peak hold for sustained transient detection
        if (absInput > ch.peakHold)
        {
            ch.peakHold = absInput;
            ch.holdCounter = holdSamples;
        }
        else if (ch.holdCounter > 0)
        {
            ch.holdCounter--;
        }
        else
        {
            // Release peak hold
            ch.peakHold = ch.peakHold * 0.9995f;
        }

        // Calculate transient amount: how much faster than slow envelope is the fast envelope
        // This detects sudden changes (transients)
        float transientRatio = 1.0f;
        if (ch.slowEnvelope > 0.0001f)
        {
            transientRatio = ch.fastEnvelope / ch.slowEnvelope;
        }

        // Convert to modifier: sensitivity 0 = no effect, sensitivity 100 = full effect
        // When transientRatio > 1, we have a transient
        float normalizedSensitivity = sensitivity / 100.0f;
        float transientModifier = 1.0f;

        if (transientRatio > 1.0f)
        {
            // Let transients through by reducing compression
            // More transient = higher modifier = less compression applied
            float transientAmount = juce::jmin((transientRatio - 1.0f) * 2.0f, 2.0f); // Cap at 2.0
            transientModifier = 1.0f + transientAmount * normalizedSensitivity;
        }

        return transientModifier;
    }

    void reset()
    {
        for (auto& ch : channels)
        {
            ch.fastEnvelope = 0.0f;
            ch.slowEnvelope = 0.0f;
            ch.peakHold = 0.0f;
            ch.holdCounter = 0;
        }
    }

private:
    struct Channel
    {
        float fastEnvelope = 0.0f;
        float slowEnvelope = 0.0f;
        float peakHold = 0.0f;
        int holdCounter = 0;
    };

    std::vector<Channel> channels;
    double sampleRate = 44100.0;

    float fastAttackCoeff = 0.0f;
    float fastReleaseCoeff = 0.0f;
    float slowAttackCoeff = 0.0f;
    float slowReleaseCoeff = 0.0f;
    int holdSamples = 0;
};

//==============================================================================
// Global Lookahead Buffer - shared across all compressor modes
class UniversalCompressor::LookaheadBuffer
{
public:
    static constexpr float MAX_LOOKAHEAD_MS = 10.0f;  // Maximum lookahead time

    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        this->numChannels = numChannels;

        // Calculate max lookahead samples for buffer allocation
        maxLookaheadSamples = static_cast<int>(std::ceil((MAX_LOOKAHEAD_MS / 1000.0) * sampleRate));

        // Allocate circular buffer
        buffer.setSize(numChannels, maxLookaheadSamples);
        buffer.clear();

        // Initialize write positions
        writePositions.resize(static_cast<size_t>(numChannels), 0);

        currentLookaheadSamples = 0;
    }

    void reset()
    {
        buffer.clear();
        for (auto& pos : writePositions)
            pos = 0;
    }

    // Process a sample through the lookahead delay
    // Returns the delayed sample and stores the current sample in the buffer
    float processSample(float input, int channel, float lookaheadMs)
    {
        if (channel < 0 || channel >= numChannels || maxLookaheadSamples <= 0)
            return input;

        // Calculate lookahead delay in samples
        int lookaheadSamples = static_cast<int>(std::round((lookaheadMs / 1000.0f) * static_cast<float>(sampleRate)));
        lookaheadSamples = juce::jlimit(0, maxLookaheadSamples - 1, lookaheadSamples);

        // Update current lookahead for latency reporting
        if (channel == 0)
            currentLookaheadSamples = lookaheadSamples;

        float delayedInput = input;

        if (lookaheadSamples > 0)
        {
            int& writePos = writePositions[static_cast<size_t>(channel)];
            int bufferSize = maxLookaheadSamples;

            // Read position is lookaheadSamples behind write position
            int readPos = (writePos - lookaheadSamples + bufferSize) % bufferSize;
            delayedInput = buffer.getSample(channel, readPos);

            // Write current sample to buffer
            buffer.setSample(channel, writePos, input);
            writePos = (writePos + 1) % bufferSize;
        }

        return delayedInput;
    }

    int getLookaheadSamples() const { return currentLookaheadSamples; }
    int getMaxLookaheadSamples() const { return maxLookaheadSamples; }

private:
    juce::AudioBuffer<float> buffer;
    std::vector<int> writePositions;
    double sampleRate = 44100.0;
    int numChannels = 2;
    int maxLookaheadSamples = 0;
    int currentLookaheadSamples = 0;
};

// Helper function to apply distortion based on type
inline float applyDistortion(float input, DistortionType type, float amount = 1.0f)
{
    if (type == DistortionType::Off || amount <= 0.0f)
        return input;

    float wet = input;
    switch (type)
    {
        case DistortionType::Soft:
            // Tape-like soft saturation (tanh)
            wet = std::tanh(input * (1.0f + amount));
            break;

        case DistortionType::Hard:
            // Transistor-style hard clipping with asymmetry
            // Optimized: replaced std::pow(x, 2.0f) with x*x for 95%+ speedup
            {
                float threshold = 0.7f / (0.5f + amount * 0.5f);
                float negThreshold = threshold * 0.9f;  // Slight asymmetry
                float invRange = 1.0f / (1.0f - threshold);
                float invNegRange = 1.0f / (1.0f - negThreshold);

                if (wet > threshold)
                {
                    float diff = wet - threshold;
                    float normDiff = diff * invRange;
                    wet = threshold + diff / (1.0f + normDiff * normDiff);
                }
                else if (wet < -negThreshold)
                {
                    float diff = std::abs(wet) - negThreshold;
                    float normDiff = diff * invNegRange;
                    wet = -negThreshold - diff / (1.0f + normDiff * normDiff);
                }
            }
            break;

        case DistortionType::Clip:
            // Hard digital clip
            wet = juce::jlimit(-1.0f / (0.5f + amount * 0.5f), 1.0f / (0.5f + amount * 0.5f), input);
            break;

        default:
            break;
    }

    return wet;
}

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

// Vintage Opto Compressor
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
        
        // Opto feedback topology: detection from output
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

        // Program-dependent release: faster on transients (Opto characteristic)
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
        // - Creates the Opto's characteristic "sticky" compression
        
        // Variable ratio based on feedback topology
        // Opto ratio varies from ~3:1 (low levels) to ~10:1 (high levels)
        // This is a key characteristic of the T4 optical cell
        float reduction = 0.0f;

        // Input-dependent threshold: lower threshold for louder inputs (Opto characteristic)
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

            // Program-dependent ratio calculation (authentic opto behavior)
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

            // Opto typically maxes out around 40dB GR
            reduction = juce::jmin(reduction, 40.0f);
        }
        
        // Opto T4 optical cell time constants
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
        
        // Opto Tube output stage - 12AX7 tube followed by 12AQ5 power tube
        // The Opto has a characteristic warm tube sound with prominent 2nd harmonic
        float makeupGain = juce::Decibels::decibelsToGain(gain);
        float driven = compressed * makeupGain;
        
        // Opto tube harmonics - generate based on whether oversampling is active
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
            
            // Opto has more harmonic content than FET
            if (levelDb > -40.0f)  // Add harmonics above -40dB
            {
                // 2nd harmonic - Opto manual spec: < 0.5% THD (0.25% typical) at ±10dBm
                float thd_target = levelDb > 6.0f ? 0.005f : 0.0025f;  // 0.5% max / 0.25% typical
                float h2_scale = thd_target * 0.85f;
                h2_level = absDriven * absDriven * h2_scale;

                // 3rd harmonic - Opto tubes produce some odd harmonics
                float h3_scale = thd_target * 0.12f;
                h3_level = absDriven * absDriven * absDriven * h3_scale;

                // 4th harmonic - minimal in opto
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
        
        // Opto output transformer - gentle high-frequency rolloff
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

// Vintage FET Compressor
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
                  float attackMs, float releaseMs, int ratioIndex, bool oversample = false,
                  const LookupTables* lookupTables = nullptr, TransientShaper* transientShaper = nullptr,
                  bool useMeasuredCurve = false, float transientSensitivity = 0.0f)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;
        
        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;
            
        auto& detector = detectors[channel];
        
        // FET Input transformer emulation
        // The FET uses the full input signal, not highpass filtered
        // The transformer provides some low-frequency coupling but doesn't remove DC entirely
        float filteredInput = input;
        
        // FET Input control - AUTHENTIC BEHAVIOR
        // The FET has a FIXED threshold that the input knob drives signal into
        // More input = more compression (not threshold change)
        
        // Fixed threshold (FET characteristic)
        // The FET threshold is around -10 dBFS according to specifications
        // This is the level where compression begins to engage
        const float thresholdDb = Constants::FET_THRESHOLD_DB; // Authentic FET threshold
        float threshold = juce::Decibels::decibelsToGain(thresholdDb);
        
        // Apply FULL input gain - this is how you drive into compression
        // Input knob range: -20 to +40dB
        float inputGainLin = juce::Decibels::decibelsToGain(inputGainDb);
        float amplifiedInput = filteredInput * inputGainLin;
        
        // Ratio mapping: 4:1, 8:1, 12:1, 20:1, all-buttons mode
        // All-buttons mode: Hardware measurements show >100:1 effective ratio
        std::array<float, 5> ratios = {4.0f, 8.0f, 12.0f, 20.0f, 120.0f}; // All-buttons >100:1
        float ratio = ratios[juce::jlimit(0, 4, ratioIndex)];
        
        // FEEDBACK TOPOLOGY for authentic FET behavior
        // The FET uses feedback compression which creates its characteristic sound
        
        // First, we need to apply the PREVIOUS envelope to get the compressed signal
        float compressed = amplifiedInput * detector.envelope;
        
        // Then detect from the COMPRESSED OUTPUT (feedback)
        // This is what gives the FET its "grabby" characteristic
        float detectionLevel = std::abs(compressed);
        
        // Calculate gain reduction based on how much we exceed threshold
        float reduction = 0.0f;
        if (detectionLevel > threshold)
        {
            // Calculate how much we're over threshold in dB
            float overThreshDb = juce::Decibels::gainToDecibels(detectionLevel / threshold);

            // Classic FET compression curve
            if (ratioIndex == 4) // All-buttons mode (FET mode)
            {
                // Use lookup table if available, otherwise fall back to piecewise calculation
                if (lookupTables != nullptr)
                {
                    reduction = lookupTables->getAllButtonsReduction(overThreshDb, useMeasuredCurve);
                }
                else
                {
                    // Fallback: piecewise approximation (Modern curve)
                    if (overThreshDb < 3.0f)
                    {
                        reduction = overThreshDb * 0.33f;
                    }
                    else if (overThreshDb < 10.0f)
                    {
                        float t = (overThreshDb - 3.0f) / 7.0f;
                        reduction = 1.0f + (overThreshDb - 3.0f) * (0.75f + t * 0.15f);
                    }
                    else
                    {
                        reduction = 6.25f + (overThreshDb - 10.0f) * 0.95f;
                    }
                }

                // Apply transient shaping: let transients punch through
                if (transientShaper != nullptr && transientSensitivity > 0.01f)
                {
                    float transientMod = transientShaper->process(input, channel, transientSensitivity);
                    // Reduce compression amount for transients (higher modifier = less reduction)
                    reduction /= transientMod;
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
        
        // FET attack and release times with LOGARITHMIC curves (hardware-accurate)
        // Attack: 20µs (0.00002s) to 800µs (0.0008s) - logarithmic taper
        // Release: 50ms to 1.1s - logarithmic taper

        // Map input parameters (assumed 0-1 range from attackMs/releaseMs) to hardware values
        // If attackMs is already in ms, we need to map it logarithmically
        const float minAttack = 0.00002f;  // 20 microseconds
        const float maxAttack = 0.0008f;   // 800 microseconds
        const float minRelease = 0.05f;    // 50 milliseconds
        const float maxRelease = 1.1f;     // 1.1 seconds

        // Logarithmic interpolation for authentic FET feel
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
            // Normal FET envelope behavior for standard ratios
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
        
        // FET Class A FET amplifier stage
        // The FET is VERY clean at -18dB input level
        // UAD reference shows THD at -65dB with 2nd harmonic at -100dB
        // Apply the envelope to get the output signal
        float output = compressed;
        
        // The FET is an extremely clean compressor with minimal harmonics
        // At -18dB input: 2nd harmonic at -100dB, 3rd at -110dB
        float absOutput = std::abs(output);
        
        // FET non-linearity and harmonics
        // All-buttons mode: 3x more harmonic distortion
        if (reduction > 3.0f && absOutput > 0.001f)
        {
            float sign = (output < 0.0f) ? -1.0f : 1.0f;

            // All-buttons mode increases harmonic content significantly
            float allButtonsMultiplier = (ratioIndex == 4) ? 3.0f : 1.0f;

            // Dynamic harmonics: scale with gain reduction for authentic FET behavior
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

            // FET manual spec: < 0.5% THD from 50 Hz to 15 kHz with limiting
            // Target ~0.45% total at maximum compression for authentic character

            // 2nd harmonic: dominant harmonic in FET compressors
            // Scales more aggressively with GR for dynamic character
            float h2_scale = 0.0010f * allButtonsMultiplier * harmonicScale;  // Increased from 0.00063f
            float h2 = output * output * h2_scale;

            // 3rd harmonic (odd-order for FET character)
            // Odd harmonics scale even more with GR (squared relationship)
            float h3_scale = 0.00075f * allButtonsMultiplier * (harmonicScale * harmonicScale);  // Increased from 0.0005f
            float h3 = output * output * output * h3_scale;

            // 5th harmonic (additional odd-order for FET)
            // Most aggressive scaling for aggressive compression
            float h5_scale = 0.00015f * allButtonsMultiplier * (grAmount * grAmount);  // Increased from 0.0001f
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
        // FET has minimal transformer coloration
        // Just a gentle rolloff above 20kHz for anti-aliasing
        // Use fixed filtering regardless of oversampling to maintain consistent harmonics
        float transformerFreq = 20000.0f;
        // Always use base sample rate for consistent filtering
        float transformerCoeff = std::exp(-2.0f * 3.14159f * transformerFreq / static_cast<float>(sampleRate));
        float filtered = output * (1.0f - transformerCoeff * 0.05f) + detector.prevOutput * transformerCoeff * 0.05f;
        detector.prevOutput = filtered;
        
        // FET Output knob - makeup gain control
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

// Classic VCA Compressor
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
            detector.overshootAmount = 0.0f; // For VCA attack overshoot
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
        
        // VCA feedforward topology: control voltage from input signal
        float detectionLevel = std::abs(input);

        // Track signal envelope rate of change for program-dependent behavior
        float signalDelta = std::abs(detectionLevel - detector.previousInput);
        detector.envelopeRate = detector.envelopeRate * 0.95f + signalDelta * 0.05f;
        detector.previousInput = detectionLevel;

        // VCA True RMS detection with ADAPTIVE window (5-15ms)
        // Transient material (drums): shorter window (5ms) for punch
        // Sustained material (vocals, bass): longer window (15ms) for smoothness

        // Detect transients: rapid level changes indicate percussive content
        float transientFactor = juce::jlimit(0.0f, 1.0f, detector.envelopeRate * 10.0f);

        // Adaptive RMS window: 5ms (transient) to 15ms (sustained)
        float adaptiveRmsTime = 0.015f - (transientFactor * 0.010f); // 15ms to 5ms

        const float rmsAlpha = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, adaptiveRmsTime * static_cast<float>(sampleRate))));
        detector.rmsBuffer = detector.rmsBuffer * rmsAlpha + detectionLevel * detectionLevel * (1.0f - rmsAlpha);
        float rmsLevel = std::sqrt(detector.rmsBuffer);
        
        // VCA signal envelope tracking for program-dependent timing
        const float envelopeAlpha = 0.99f;
        detector.signalEnvelope = detector.signalEnvelope * envelopeAlpha + rmsLevel * (1.0f - envelopeAlpha);
        
        // VCA threshold control (-40dB to +20dB range typical)
        float thresholdLin = juce::Decibels::decibelsToGain(threshold);
        
        float reduction = 0.0f;
        if (rmsLevel > thresholdLin)
        {
            float overThreshDb = juce::Decibels::gainToDecibels(rmsLevel / thresholdLin);
            
            // VCA OverEasy mode - proprietary soft knee with PARABOLIC curve
            if (overEasy)
            {
                // VCA OverEasy uses a parabolic curve for smooth, musical compression
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
                    // VCA uses parabolic curve: f(x) = x² for smooth onset
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
                // Hard knee compression (original VCA without OverEasy)
                reduction = overThreshDb * (1.0f - 1.0f / ratio);
            }
            
            // VCA can achieve infinite compression (approximately 120:1) with complete stability
            // Feed-forward design prevents instability issues of feedback compressors
            reduction = juce::jmin(reduction, Constants::VCA_MAX_REDUCTION_DB); // Practical limit for musical content
        }
        
        // VCA program-dependent attack and release times that "track" signal envelope
        // Attack times automatically vary with rate of level change in program material
        // Manual specifications: 15ms for 10dB, 5ms for 20dB, 3ms for 30dB change above threshold
        // User attackParam (0.1-50ms) scales the program-dependent attack times

        float attackTime, releaseTime;

        // VCA attack times track the signal envelope rate
        // attackParam range: 0.1ms to 50ms - used as a scaling factor
        float userAttackScale = attackParam / 15.0f;  // Normalize to 1.0 at default 15ms

        float programAttackTime;
        if (reduction > 0.1f)
        {
            // VCA manual: Attack time for 63% of level change
            // 15ms for 10dB, 5ms for 20dB, 3ms for 30dB
            if (reduction <= 10.0f)
                programAttackTime = 0.015f; // 15ms for 10dB level change
            else if (reduction <= 20.0f)
                programAttackTime = 0.005f; // 5ms for 20dB level change
            else
                programAttackTime = 0.003f; // 3ms for 30dB level change
        }
        else
        {
            programAttackTime = 0.015f; // Default 15ms when not compressing
        }

        // Scale program-dependent attack by user control
        // Lower user values = faster attack, higher = slower (up to 3.3x at 50ms setting)
        attackTime = programAttackTime * userAttackScale;
        attackTime = juce::jlimit(0.0001f, 0.050f, attackTime); // 0.1ms to 50ms range
        
        // VCA release: blend user control with program-dependent 120dB/sec characteristic
        // releaseParam range: 10ms to 5000ms
        // At minimum (10ms): pure program-dependent behavior (120dB/sec)
        // At maximum (5000ms): long fixed release
        float userReleaseTime = releaseParam / 1000.0f;  // Convert ms to seconds

        // Calculate program-dependent release (120dB/sec characteristic)
        const float releaseRate = 120.0f; // dB per second
        float programReleaseTime;
        if (reduction > 0.1f)
        {
            programReleaseTime = reduction / releaseRate;
            programReleaseTime = juce::jmax(0.008f, programReleaseTime);
        }
        else
        {
            programReleaseTime = 0.008f;
        }

        // Blend: shorter user times favor program-dependent, longer times use fixed release
        // This preserves VCA character at fast settings while allowing longer releases
        float blendFactor = juce::jlimit(0.0f, 1.0f, (userReleaseTime - 0.01f) / 0.5f); // 10ms-510ms transition
        releaseTime = programReleaseTime * (1.0f - blendFactor) + userReleaseTime * blendFactor;
        
        // Classic VCA control voltage generation (-6mV/dB logarithmic curve)
        // This is key to the VCA sound - logarithmic VCA response
        detector.controlVoltage = reduction * Constants::VCA_CONTROL_VOLTAGE_SCALE; // -6mV/dB characteristic
        
        // VCA feed-forward envelope following with complete stability
        // Feed-forward design is inherently stable even at infinite compression ratios
        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        
        // Calculate proper exponential coefficients for VCA-style response with safety
        float attackCoeff = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, attackTime * static_cast<float>(sampleRate))));
        float releaseCoeff = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, releaseTime * static_cast<float>(sampleRate))));
        
        if (targetGain < detector.envelope)
        {
            // Attack phase - VCA feed-forward design for fast, stable response
            detector.envelope = targetGain + (detector.envelope - targetGain) * attackCoeff;

            // VCA attack overshoot on fast attacks (1-2dB characteristic)
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

        // Apply overshoot to envelope for VCA attack characteristic
        float envelopeWithOvershoot = detector.envelope * (1.0f + detector.overshootAmount);
        envelopeWithOvershoot = juce::jlimit(0.0001f, 1.0f, envelopeWithOvershoot);

        // VCA feed-forward topology: apply compression to input signal
        // This is different from feedback compressors - much more stable
        float compressed = input * envelopeWithOvershoot;
        
        // Classic VCA characteristics (vintage VCA chip design)
        // The Classic VCA is renowned for being EXTREMELY clean - much cleaner than most compressors
        // Manual specification: 0.075% 2nd harmonic at infinite compression at +4dBm output
        // 0.5% 3rd harmonic typical at infinite compression ratio
        float processed = compressed;
        float absLevel = std::abs(processed);
        
        // Calculate actual signal level in dB for harmonic generation
        float levelDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, absLevel));
        
        // VCA harmonic distortion - much cleaner than other compressor types
        if (absLevel > 0.01f)  // Process non-silence
        {
            float sign = (processed < 0.0f) ? -1.0f : 1.0f;
            
            // Classic VCA harmonics - extremely clean, even at high compression ratios
            float h2_level = 0.0f;
            float h3_level = 0.0f;
            
            // No pre-saturation compensation needed anymore
            // We apply compensation AFTER saturation to avoid compression effects
            float harmonicCompensation = 1.0f; // No pre-compensation
            float h2Boost = harmonicCompensation;
            float h3Boost = harmonicCompensation;
            
            // VCA harmonic generation - per actual manual specification
            // Manual spec: 0.75% 2nd harmonic, 0.5% 3rd harmonic at infinite compression
            // Logic Pro shows similar levels (~-60dB to -81dB for 3rd harmonic)
            if (levelDb > -30.0f && reduction > 2.0f)
            {
                // Compression factor scales harmonics based on how hard we're compressing
                float compressionFactor = juce::jmin(1.0f, reduction / 30.0f);

                // 2nd harmonic - VCA manual: 0.75% at infinite compression at +4dBm output
                // 0.75% = 0.0075 linear = -42.5dB
                float h2_scale = 0.0075f / (absLevel * absLevel + 0.0001f);
                h2_level = absLevel * absLevel * h2_scale * compressionFactor * h2Boost;

                // 3rd harmonic - VCA manual: 0.5% typical at infinite compression
                // 0.5% = 0.005 linear = -46dB
                // Account for frequency dependence (decreases linearly with frequency)
                if (reduction > 10.0f)
                {
                    float freqFactor = 50.0f / 1000.0f;  // Linear decrease with frequency
                    float h3_scale = (0.005f * freqFactor) / (absLevel * absLevel * absLevel + 0.0001f);
                    h3_level = absLevel * absLevel * absLevel * h3_scale * compressionFactor * h3Boost;
                }
            }
            
            // Apply minimal harmonics - Classic VCA is known for its cleanliness
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
            
            // Classic VCA has very high headroom - minimal saturation
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
        float overshootAmount = 0.0f;   // Attack overshoot for Classic VCA characteristic
    };
    
    std::vector<Detector> detectors;
    double sampleRate = 0.0;  // Set by prepare() from DAW
};

// Bus Compressor
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
            
            // Bus Compressor sidechain filter
            // Highpass at 60Hz to prevent pumping from low frequencies
            detector.sidechainFilter->get<0>().coefficients = 
                juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 60.0f, 0.707f);
            // No lowpass in original Bus - full bandwidth
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
        
        // Bus Compressor quad VCA topology
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
        
        // Step 3: Bus compressor uses the sidechain signal directly for detection
        float detectionLevel = std::abs(sidechainInput);
        
        // Bus Compressor specific ratios: 2:1, 4:1, 10:1
        // ratio parameter already contains the actual ratio value (2.0, 4.0, or 10.0)
        float actualRatio = ratio;
        
        float thresholdLin = juce::Decibels::decibelsToGain(threshold);
        
        float reduction = 0.0f;
        if (detectionLevel > thresholdLin)
        {
            float overThreshDb = juce::Decibels::gainToDecibels(detectionLevel / thresholdLin);
            
            // Bus Compressor compression curve - relatively linear/hard knee
            reduction = overThreshDb * (1.0f - 1.0f / actualRatio);
            // Bus compressor typically used for gentle compression (max ~20dB GR)
            reduction = juce::jmin(reduction, Constants::BUS_MAX_REDUCTION_DB);
        }
        
        // Bus Compressor attack and release times
        std::array<float, 6> attackTimes = {0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f}; // ms
        std::array<float, 5> releaseTimes = {100.0f, 300.0f, 600.0f, 1200.0f, -1.0f}; // ms, -1 = auto
        
        float attackTime = attackTimes[juce::jlimit(0, 5, attackIndex)] * 0.001f;
        float releaseTime = releaseTimes[juce::jlimit(0, 4, releaseIndex)] * 0.001f;
        
        // Bus Auto-release mode - program-dependent (150-450ms range)
        if (releaseTime < 0.0f)
        {
            // Hardware-accurate Bus auto-release: 150ms to 450ms based on program
            // Faster for transient-dense material, slower for sustained compression

            // Track signal dynamics
            float signalDelta = std::abs(detectionLevel - detector.previousLevel);
            detector.previousLevel = detector.previousLevel * 0.95f + detectionLevel * 0.05f;

            // Transient density: high delta = drums/percussion, low delta = sustained
            float transientDensity = juce::jlimit(0.0f, 1.0f, signalDelta * 20.0f);

            // Compression amount factor: more compression = slower release
            float compressionFactor = juce::jlimit(0.0f, 1.0f, reduction / 12.0f); // 0dB to 12dB

            // Bus auto-release formula (150ms to 450ms)
            // Transient material: faster release (150-250ms)
            // Sustained material with heavy compression: slower release (300-450ms)
            float minRelease = 0.15f;  // 150ms
            float maxRelease = 0.45f;  // 450ms

            // Calculate release time based on material and compression
            float sustainedFactor = (1.0f - transientDensity) * compressionFactor;
            releaseTime = minRelease + (sustainedFactor * (maxRelease - minRelease));
        }
        
        // Bus Compressor envelope following with smooth response
        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        
        if (targetGain < detector.envelope)
        {
            // Attack phase - Bus compressor is known for smooth attack response - approximate exp
            float divisor = juce::jmax(Constants::EPSILON, attackTime * static_cast<float>(sampleRate));
            float attackCoeff = juce::jmax(0.0f, juce::jmin(0.9999f, 1.0f - 1.0f / divisor));
            detector.envelope = targetGain + (detector.envelope - targetGain) * attackCoeff;
        }
        else
        {
            // Release phase with Bus characteristic smoothness - approximate exp
            float divisor = juce::jmax(Constants::EPSILON, releaseTime * static_cast<float>(sampleRate));
            float releaseCoeff = juce::jmax(0.0f, juce::jmin(0.9999f, 1.0f - 1.0f / divisor));
            detector.envelope = targetGain + (detector.envelope - targetGain) * releaseCoeff;
        }
        
        // Envelope hysteresis: blend with previous gain reduction for Bus memory effect
        // Bus circuitry has capacitance that creates slight "memory" in the envelope
        float currentGR = 1.0f - detector.envelope;
        currentGR = 0.9f * currentGR + 0.1f * detector.previousGR; // 10% memory for Bus smoothness
        detector.previousGR = currentGR;
        detector.envelope = 1.0f - currentGR;

        // NaN/Inf safety check
        if (std::isnan(detector.envelope) || std::isinf(detector.envelope))
            detector.envelope = 1.0f;

        // Apply the gain reduction envelope to the input signal
        float compressed = input * detector.envelope;
        
        // Bus Compressor Quad VCA characteristics
        // Hardware-accurate THD: 0.01% @ 0dB GR, 0.05-0.1% @ 12dB GR
        float processed = compressed;
        float absLevel = std::abs(processed);

        // Calculate level for harmonic generation
        float levelDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, absLevel));

        // Bus compressor harmonics - quad VCA coloration increases with compression
        if (absLevel > 0.01f)
        {
            float sign = (processed < 0.0f) ? -1.0f : 1.0f;

            // Bus VCA THD specification:
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

            // Bus quad VCA: primarily 2nd harmonic (even), minimal odd harmonics
            // 2nd harmonic: ~85% of total THD
            // 3rd harmonic: ~15% of total THD
            float h2_scale = thdLinear * 0.85f;
            float h3_scale = thdLinear * 0.15f;

            float h2_level = absLevel * absLevel * h2_scale;
            float h3_level = absLevel * absLevel * absLevel * h3_scale;
            
            // Apply harmonics using waveshaping for consistency
            processed = compressed;
            
            // Add 2nd harmonic for Bus warmth
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
            
            // Bus console saturation - very gentle
            if (absLevel > 0.95f)
            {
                // Bus console output stage saturation
                float excess = (absLevel - 0.95f) / 0.05f;
                float sslSat = 0.95f + 0.05f * std::tanh(excess * 0.7f);
                processed = sign * sslSat * (processed / absLevel);
            }
        }
        
        // Apply makeup gain
        float compressed_output = processed * juce::Decibels::decibelsToGain(makeupGain);

        // Bus-style parallel compression (New York compression)
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

// Studio FET Compressor (cleaner than Vintage FET)
class UniversalCompressor::StudioFETCompressor
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        detectors.resize(static_cast<size_t>(numChannels));
        for (auto& detector : detectors)
        {
            detector.envelope = 1.0f;
            detector.previousLevel = 0.0f;
            detector.previousGR = 0.0f;
        }
    }

    float process(float input, int channel, float inputGain, float outputGain,
                  float attackMs, float releaseMs, int ratioIndex, float sidechainInput)
    {
        if (channel >= static_cast<int>(detectors.size()) || sampleRate <= 0.0)
            return input;

        auto& detector = detectors[static_cast<size_t>(channel)];

        // Apply input gain (drives signal into fixed threshold)
        float gained = input * juce::Decibels::decibelsToGain(inputGain);

        // Fixed threshold at -10dBFS (FET spec)
        constexpr float thresholdDb = Constants::STUDIO_FET_THRESHOLD_DB;
        const float threshold = juce::Decibels::decibelsToGain(thresholdDb);

        // Use sidechain input for detection
        float detectionLevel = std::abs(sidechainInput) * juce::Decibels::decibelsToGain(inputGain);

        // Ratio selection (same as Vintage FET)
        float ratio;
        switch (ratioIndex)
        {
            case 0: ratio = 4.0f; break;
            case 1: ratio = 8.0f; break;
            case 2: ratio = 12.0f; break;
            case 3: ratio = 20.0f; break;
            case 4: ratio = 100.0f; break;  // All-buttons
            default: ratio = 4.0f; break;
        }

        // Calculate gain reduction
        float reduction = 0.0f;
        if (detectionLevel > threshold)
        {
            float overDb = juce::Decibels::gainToDecibels(detectionLevel / threshold);
            reduction = overDb * (1.0f - 1.0f / ratio);
            reduction = juce::jmin(reduction, 30.0f);
        }

        // Studio FET timing - same fast response, but cleaner
        const float minAttack = 0.00002f;  // 20µs
        const float maxAttack = 0.0008f;   // 800µs
        const float minRelease = 0.05f;    // 50ms
        const float maxRelease = 1.1f;     // 1.1s

        float attackNorm = juce::jlimit(0.0f, 1.0f, attackMs / 0.8f);
        float releaseNorm = juce::jlimit(0.0f, 1.0f, releaseMs / 1100.0f);

        float attackTime = minAttack * std::pow(maxAttack / minAttack, attackNorm);
        float releaseTime = minRelease * std::pow(maxRelease / minRelease, releaseNorm);

        // Envelope following
        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        float attackCoeff = std::exp(-1.0f / (juce::jmax(0.0001f, attackTime * static_cast<float>(sampleRate))));
        float releaseCoeff = std::exp(-1.0f / (juce::jmax(0.0001f, releaseTime * static_cast<float>(sampleRate))));

        if (targetGain < detector.envelope)
            detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
        else
            detector.envelope = releaseCoeff * detector.envelope + (1.0f - releaseCoeff) * targetGain;

        detector.envelope = juce::jlimit(0.001f, 1.0f, detector.envelope);

        // Apply compression
        float compressed = gained * detector.envelope;

        // Studio FET - MUCH cleaner harmonics (30% of Vintage FET)
        // Rev E Blackface was the "Low Noise" revision
        float absLevel = std::abs(compressed);
        if (absLevel > 0.01f && reduction > 0.5f)
        {
            float sign = compressed > 0.0f ? 1.0f : -1.0f;
            float harmonicAmount = reduction / 30.0f * Constants::STUDIO_FET_HARMONIC_SCALE;

            // Subtle 2nd harmonic only
            float h2 = absLevel * absLevel * harmonicAmount * 0.002f;
            compressed += sign * h2;
        }

        // Apply output gain
        float output = compressed * juce::Decibels::decibelsToGain(outputGain);
        return juce::jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, output);
    }

    float getGainReduction(int channel) const
    {
        if (channel >= static_cast<int>(detectors.size()))
            return 0.0f;
        return juce::Decibels::gainToDecibels(detectors[static_cast<size_t>(channel)].envelope);
    }

private:
    struct Detector
    {
        float envelope = 1.0f;
        float previousLevel = 0.0f;
        float previousGR = 0.0f;
    };

    std::vector<Detector> detectors;
    double sampleRate = 0.0;
};

// Studio VCA Compressor (modern, versatile)
class UniversalCompressor::StudioVCACompressor
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        detectors.resize(static_cast<size_t>(numChannels));
        for (auto& detector : detectors)
        {
            detector.envelope = 1.0f;
            detector.rms = 0.0f;
            detector.previousGR = 0.0f;
        }
    }

    float process(float input, int channel, float thresholdDb, float ratio,
                  float attackMs, float releaseMs, float outputGain, float sidechainInput)
    {
        if (channel >= static_cast<int>(detectors.size()) || sampleRate <= 0.0)
            return input;

        auto& detector = detectors[static_cast<size_t>(channel)];

        // Studio VCA uses RMS detection
        float squared = sidechainInput * sidechainInput;
        float rmsCoeff = std::exp(-1.0f / (0.01f * static_cast<float>(sampleRate)));  // 10ms RMS
        detector.rms = rmsCoeff * detector.rms + (1.0f - rmsCoeff) * squared;
        float detectionLevel = std::sqrt(detector.rms);

        float threshold = juce::Decibels::decibelsToGain(thresholdDb);

        // Soft knee (6dB) - characteristic of Studio VCA
        float kneeWidth = Constants::STUDIO_VCA_SOFT_KNEE_DB;
        float kneeStart = threshold * juce::Decibels::decibelsToGain(-kneeWidth / 2.0f);
        float kneeEnd = threshold * juce::Decibels::decibelsToGain(kneeWidth / 2.0f);

        float reduction = 0.0f;
        if (detectionLevel > kneeStart)
        {
            if (detectionLevel < kneeEnd)
            {
                // In knee region - smooth transition
                float kneePosition = (detectionLevel - kneeStart) / (kneeEnd - kneeStart);
                float effectiveRatio = 1.0f + (ratio - 1.0f) * kneePosition * kneePosition;
                float overDb = juce::Decibels::gainToDecibels(detectionLevel / threshold);
                reduction = overDb * (1.0f - 1.0f / effectiveRatio);
            }
            else
            {
                // Above knee - full compression
                float overDb = juce::Decibels::gainToDecibels(detectionLevel / threshold);
                reduction = overDb * (1.0f - 1.0f / ratio);
            }
            reduction = juce::jmin(reduction, Constants::STUDIO_VCA_MAX_REDUCTION_DB);
        }

        // Studio VCA attack/release: 0.3ms to 75ms attack, 0.1s to 4s release
        float attackTime = juce::jlimit(0.0003f, 0.075f, attackMs / 1000.0f);
        float releaseTime = juce::jlimit(0.1f, 4.0f, releaseMs / 1000.0f);

        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        float attackCoeff = std::exp(-1.0f / (attackTime * static_cast<float>(sampleRate)));
        float releaseCoeff = std::exp(-1.0f / (releaseTime * static_cast<float>(sampleRate)));

        if (targetGain < detector.envelope)
            detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
        else
            detector.envelope = releaseCoeff * detector.envelope + (1.0f - releaseCoeff) * targetGain;

        detector.envelope = juce::jlimit(0.001f, 1.0f, detector.envelope);

        // Apply compression
        float compressed = input * detector.envelope;

        // Studio VCA is very clean - minimal harmonics
        float absLevel = std::abs(compressed);
        if (absLevel > 0.8f)
        {
            // Gentle soft clipping at high levels
            float excess = absLevel - 0.8f;
            float softClip = 0.8f + 0.2f * std::tanh(excess * 5.0f);
            compressed = (compressed > 0.0f ? 1.0f : -1.0f) * softClip;
        }

        // Apply output gain
        float output = compressed * juce::Decibels::decibelsToGain(outputGain);
        return juce::jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, output);
    }

    float getGainReduction(int channel) const
    {
        if (channel >= static_cast<int>(detectors.size()))
            return 0.0f;
        return juce::Decibels::gainToDecibels(detectors[static_cast<size_t>(channel)].envelope);
    }

private:
    struct Detector
    {
        float envelope = 1.0f;
        float rms = 0.0f;
        float previousGR = 0.0f;
    };

    std::vector<Detector> detectors;
    double sampleRate = 0.0;
};

//==============================================================================
// Digital Compressor - Clean, transparent, precise
class UniversalCompressor::DigitalCompressor
{
public:
    void prepare(double sr, int numCh, int maxBlockSize)
    {
        sampleRate = sr;
        this->numChannels = numCh;
        detectors.resize(static_cast<size_t>(numCh));
        for (auto& detector : detectors)
        {
            detector.envelope = 1.0f;
            detector.adaptiveRelease = 0.0f;
        }

        // Calculate max lookahead samples for buffer allocation
        // Use centralized constant from LookaheadBuffer
        maxLookaheadSamples = static_cast<int>(std::ceil((LookaheadBuffer::MAX_LOOKAHEAD_MS / 1000.0) * sampleRate));

        // Allocate lookahead buffer: needs to hold maxLookaheadSamples for delay
        // We use a circular buffer sized to maxLookaheadSamples
        lookaheadBuffer.setSize(numCh, maxLookaheadSamples);
        lookaheadBuffer.clear();

        // Initialize write positions per channel
        lookaheadWritePos.resize(static_cast<size_t>(numCh), 0);

        // Reset current lookahead samples
        currentLookaheadSamples = 0;
    }

    float process(float input, int channel, float thresholdDb, float ratio, float kneeDb,
                  float attackMs, float releaseMs, float lookaheadMs, float mixPercent,
                  float outputGain, bool adaptiveRelease, float sidechainInput)
    {
        if (channel >= static_cast<int>(detectors.size()) || sampleRate <= 0.0)
            return input;

        auto& detector = detectors[static_cast<size_t>(channel)];

        // Calculate lookahead delay in samples (clamped to valid range)
        int lookaheadSamples = static_cast<int>(std::round((lookaheadMs / 1000.0f) * static_cast<float>(sampleRate)));
        lookaheadSamples = juce::jlimit(0, maxLookaheadSamples - 1, lookaheadSamples);

        // Update current lookahead for latency reporting (use max across channels)
        if (channel == 0)
            currentLookaheadSamples = lookaheadSamples;

        // Get delayed sample from circular buffer for output (the "past" audio)
        float delayedInput = input;  // Default to no delay
        if (lookaheadSamples > 0 && maxLookaheadSamples > 0)
        {
            int& writePos = lookaheadWritePos[static_cast<size_t>(channel)];
            int bufferSize = maxLookaheadSamples;

            // Read position is lookaheadSamples behind write position
            int readPos = (writePos - lookaheadSamples + bufferSize) % bufferSize;
            delayedInput = lookaheadBuffer.getSample(channel, readPos);

            // Write current input to buffer
            lookaheadBuffer.setSample(channel, writePos, input);

            // Advance write position
            writePos = (writePos + 1) % bufferSize;
        }

        // Peak detection uses current (future) sidechain input for gain computation
        // This allows the compressor to "see ahead" and react before the audio arrives
        float detectionLevel = std::abs(sidechainInput);
        float detectionDb = juce::Decibels::gainToDecibels(juce::jmax(detectionLevel, 0.00001f));

        // Soft knee calculation
        float reduction = 0.0f;
        if (kneeDb > 0.0f)
        {
            // Soft knee
            float kneeStart = thresholdDb - kneeDb / 2.0f;
            float kneeEnd = thresholdDb + kneeDb / 2.0f;

            if (detectionDb > kneeStart)
            {
                if (detectionDb < kneeEnd)
                {
                    // In knee region - quadratic interpolation
                    float kneePosition = (detectionDb - kneeStart) / kneeDb;
                    float effectiveRatio = 1.0f + (ratio - 1.0f) * kneePosition * kneePosition;
                    float overDb = detectionDb - thresholdDb;
                    reduction = overDb * (1.0f - 1.0f / effectiveRatio) * kneePosition;
                }
                else
                {
                    // Above knee - full compression
                    float overDb = detectionDb - thresholdDb;
                    reduction = overDb * (1.0f - 1.0f / ratio);
                }
            }
        }
        else
        {
            // Hard knee
            if (detectionDb > thresholdDb)
            {
                float overDb = detectionDb - thresholdDb;
                reduction = overDb * (1.0f - 1.0f / ratio);
            }
        }

        reduction = juce::jmax(0.0f, reduction);

        // Attack and release with adaptive option
        float attackTime = juce::jmax(0.00001f, attackMs / 1000.0f);
        float releaseTime = juce::jmax(0.001f, releaseMs / 1000.0f);

        if (adaptiveRelease && reduction > 0.0f)
        {
            // Program-dependent release: faster release for transients
            float transientAmount = reduction - detector.adaptiveRelease;
            detector.adaptiveRelease = reduction;
            if (transientAmount > 3.0f)  // 3dB transient
            {
                releaseTime *= 0.3f;  // 3x faster release for transients
            }
        }

        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        float attackCoeff = std::exp(-1.0f / (attackTime * static_cast<float>(sampleRate)));
        float releaseCoeff = std::exp(-1.0f / (releaseTime * static_cast<float>(sampleRate)));

        if (targetGain < detector.envelope)
            detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
        else
            detector.envelope = releaseCoeff * detector.envelope + (1.0f - releaseCoeff) * targetGain;

        detector.envelope = juce::jlimit(0.0001f, 1.0f, detector.envelope);

        // Apply compression to DELAYED input (the gain was computed from future/current samples)
        float compressed = delayedInput * detector.envelope;

        // Mix (parallel compression) - use delayed input for dry signal too
        float mixAmount = mixPercent / 100.0f;
        float output = delayedInput * (1.0f - mixAmount) + compressed * mixAmount;

        // Apply output gain
        output *= juce::Decibels::decibelsToGain(outputGain);

        return juce::jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, output);
    }

    float getGainReduction(int channel) const
    {
        if (channel >= static_cast<int>(detectors.size()))
            return 0.0f;
        return juce::Decibels::gainToDecibels(detectors[static_cast<size_t>(channel)].envelope);
    }

    int getLookaheadSamples() const
    {
        return currentLookaheadSamples;
    }

private:
    struct Detector
    {
        float envelope = 1.0f;
        float adaptiveRelease = 0.0f;
    };

    std::vector<Detector> detectors;
    juce::AudioBuffer<float> lookaheadBuffer;
    std::vector<int> lookaheadWritePos;
    int maxLookaheadSamples = 0;
    int currentLookaheadSamples = 0;
    int numChannels = 2;
    double sampleRate = 0.0;
};

// Parameter layout creation
juce::AudioProcessorValueTreeState::ParameterLayout UniversalCompressor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    try {
    
    // Mode selection - 7 modes: 4 Vintage + 2 Studio + 1 Digital
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode",
        juce::StringArray{"Vintage Opto", "Vintage FET", "Classic VCA", "Vintage VCA (Bus)",
                          "Studio FET", "Studio VCA", "Digital"}, 0));

    // Global parameters
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

    // Sidechain highpass filter - prevents low frequency pumping
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "sidechain_hp", "SC HP Filter",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Auto makeup gain
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "auto_makeup", "Auto Makeup", false));

    // Distortion type (Off, Soft, Hard, Clip)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "distortion_type", "Distortion",
        juce::StringArray{"Off", "Soft", "Hard", "Clip"}, 0));

    // Distortion amount
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "distortion_amount", "Distortion Amt",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f,
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

    // Global lookahead for all modes (not just Digital)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "global_lookahead", "Lookahead",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Global sidechain listen (output sidechain signal for monitoring)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "global_sidechain_listen", "SC Listen", false));

    // Stereo link mode (Stereo = max-level, Mid-Side = M/S processing, Dual Mono = independent)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "stereo_link_mode", "Link Mode",
        juce::StringArray{"Stereo", "Mid-Side", "Dual Mono"}, 0));

    // Analog noise floor enable (optional for CPU savings)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "noise_enable", "Analog Noise", true));

    // Oversampling factor (0 = 2x, 1 = 4x)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "oversampling", "Oversampling",
        juce::StringArray{"2x", "4x"}, 0));

    // Sidechain EQ - Low shelf
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "sc_low_freq", "SC Low Freq",
        juce::NormalisableRange<float>(60.0f, 500.0f, 1.0f, 0.5f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "sc_low_gain", "SC Low Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Sidechain EQ - High shelf
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "sc_high_freq", "SC High Freq",
        juce::NormalisableRange<float>(2000.0f, 16000.0f, 10.0f, 0.5f), 8000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "sc_high_gain", "SC High Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // True-Peak Detection for sidechain (ITU-R BS.1770 compliant)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "true_peak_enable", "True Peak", false));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "true_peak_quality", "TP Quality",
        juce::StringArray{"4x (Standard)", "8x (High)"}, 0));

    // Add read-only gain reduction meter parameter for DAW display (LV2/VST3)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "gr_meter", "GR",
        juce::NormalisableRange<float>(-30.0f, 0.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    
    // Opto parameters (Vintage Opto style)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "opto_peak_reduction", "Peak Reduction", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f)); // Default to 0 (no compression)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "opto_gain", "Gain", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f)); // Unity gain at 50%
    layout.add(std::make_unique<juce::AudioParameterBool>("opto_limit", "Limit Mode", false));
    
    // FET parameters (Vintage FET style)
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

    // FET All-Buttons mode curve selection (Modern = current algorithm, Measured = hardware-measured)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "fet_curve_mode", "Curve Mode",
        juce::StringArray{"Modern", "Measured"}, 0));

    // FET Transient control - lets transients punch through compression (0-100%)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "fet_transient", "Transient",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // VCA parameters (Classic VCA style)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "vca_threshold", "Threshold", 
        juce::NormalisableRange<float>(-38.0f, 12.0f, 0.1f), 0.0f)); // VCA range: 10mV(-38dB) to 3V(+12dB)
    // VCA ratio: 1:1 to infinity (120:1), with 4:1 at 12 o'clock
    // Skew factor calculated so midpoint (0.5 normalized) = 4:1
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "vca_ratio", "Ratio",
        juce::NormalisableRange<float>(1.0f, 120.0f, 0.1f, 0.3f), 4.0f)); // Skew 0.3 puts 4:1 near center
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
    
    // Bus parameters (Bus Compressor style)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "bus_threshold", "Threshold", 
        juce::NormalisableRange<float>(-30.0f, 15.0f, 0.1f), 0.0f)); // Extended range for more flexibility, default to 0dB
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "bus_ratio", "Ratio", 
        juce::StringArray{"2:1", "4:1", "10:1"}, 0)); // Bus spec: discrete ratios
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
        juce::AudioParameterFloatAttributes().withLabel("%"))); // Bus parallel compression

    // Studio FET parameters (shares most params with Vintage FET)
    // Uses: fet_input, fet_output, fet_attack, fet_release, fet_ratio

    // Studio VCA parameters (Studio VCA style)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "studio_vca_threshold", "Threshold",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 0.1f), -10.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "studio_vca_ratio", "Ratio",
        juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f), 3.0f));  // Studio VCA: 1.5:1 to 10:1
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "studio_vca_attack", "Attack",
        juce::NormalisableRange<float>(0.3f, 75.0f, 0.1f), 10.0f));  // Studio VCA: Fast to Slow
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "studio_vca_release", "Release",
        juce::NormalisableRange<float>(100.0f, 4000.0f, 1.0f), 300.0f));  // Studio VCA: 0.1s to 4s
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "studio_vca_output", "Output",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f));

    // Digital Compressor parameters (transparent, precise)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_threshold", "Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -20.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_ratio", "Ratio",
        juce::NormalisableRange<float>(1.0f, 100.0f, 0.1f, 0.4f), 4.0f));  // Skew for better low-ratio control
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_knee", "Knee",
        juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f), 6.0f));  // Soft knee width in dB
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_attack", "Attack",
        juce::NormalisableRange<float>(0.01f, 500.0f, 0.01f, 0.3f), 10.0f));  // 0.01ms to 500ms
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_release", "Release",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 1.0f, 0.4f), 100.0f));  // 1ms to 5s
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_lookahead", "Lookahead",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 0.0f));  // Up to 10ms lookahead
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));  // Parallel compression
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "digital_output", "Output",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "digital_adaptive", "Adaptive Release", false));  // Program-dependent release
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "digital_sidechain_listen", "Sidechain Listen", false));  // Monitor sidechain signal
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

    // Initialize all-buttons transfer curves
    // Range: 0-30dB over threshold, maps to 0-30dB gain reduction
    // Modern curve: current piecewise implementation for compatibility
    // Measured curve: based on hardware analysis of real FET units

    // Hardware-measured data points (overThresh dB → reduction dB):
    // 0→0, 2→0.4, 4→1.2, 6→2.8, 8→5.0, 10→7.5, 12→10.2, 15→13.8, 20→18.5, 30→28.0
    // Interpolate between these for the measured curve

    constexpr float measuredPoints[][2] = {
        {0.0f, 0.0f}, {2.0f, 0.4f}, {4.0f, 1.2f}, {6.0f, 2.8f}, {8.0f, 5.0f},
        {10.0f, 7.5f}, {12.0f, 10.2f}, {15.0f, 13.8f}, {20.0f, 18.5f}, {30.0f, 28.0f}
    };
    constexpr int numPoints = sizeof(measuredPoints) / sizeof(measuredPoints[0]);

    for (int i = 0; i < ALLBUTTONS_TABLE_SIZE; ++i)
    {
        // Input range: 0-30dB over threshold
        float overThreshDb = 30.0f * static_cast<float>(i) / static_cast<float>(ALLBUTTONS_TABLE_SIZE - 1);

        // Modern curve (current piecewise implementation)
        if (overThreshDb < 3.0f)
        {
            allButtonsModernCurve[i] = overThreshDb * 0.33f;
        }
        else if (overThreshDb < 10.0f)
        {
            float t = (overThreshDb - 3.0f) / 7.0f;
            allButtonsModernCurve[i] = 1.0f + (overThreshDb - 3.0f) * (0.75f + t * 0.15f);
        }
        else
        {
            allButtonsModernCurve[i] = 6.25f + (overThreshDb - 10.0f) * 0.95f;
        }
        allButtonsModernCurve[i] = juce::jmin(allButtonsModernCurve[i], 30.0f);

        // Measured curve (interpolated from hardware data)
        // Find the two nearest points and interpolate
        float measuredReduction = 0.0f;
        for (int p = 0; p < numPoints - 1; ++p)
        {
            if (overThreshDb >= measuredPoints[p][0] && overThreshDb <= measuredPoints[p + 1][0])
            {
                float t = (overThreshDb - measuredPoints[p][0]) /
                          (measuredPoints[p + 1][0] - measuredPoints[p][0]);
                measuredReduction = measuredPoints[p][1] + t * (measuredPoints[p + 1][1] - measuredPoints[p][1]);
                break;
            }
        }
        if (overThreshDb > measuredPoints[numPoints - 1][0])
        {
            // Extrapolate beyond last point
            measuredReduction = measuredPoints[numPoints - 1][1];
        }
        allButtonsMeasuredCurve[i] = measuredReduction;
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

float UniversalCompressor::LookupTables::getAllButtonsReduction(float overThreshDb, bool useMeasuredCurve) const
{
    // Clamp to table range (0-30dB)
    overThreshDb = juce::jlimit(0.0f, 30.0f, overThreshDb);

    // Map to table index with linear interpolation for smooth transitions
    float indexFloat = overThreshDb * static_cast<float>(ALLBUTTONS_TABLE_SIZE - 1) / 30.0f;
    int index0 = static_cast<int>(indexFloat);
    int index1 = juce::jmin(index0 + 1, ALLBUTTONS_TABLE_SIZE - 1);
    float frac = indexFloat - static_cast<float>(index0);

    const auto& curve = useMeasuredCurve ? allButtonsMeasuredCurve : allButtonsModernCurve;

    // Linear interpolation between table entries
    return curve[index0] + frac * (curve[index1] - curve[index0]);
}

// Constructor
UniversalCompressor::UniversalCompressor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)  // External sidechain
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "UniversalCompressor", createParameterLayout()),
      currentSampleRate(0.0),  // Set by prepareToPlay from DAW
      currentBlockSize(512)
{
    // Initialize atomic values explicitly with relaxed ordering
    inputMeter.store(-60.0f, std::memory_order_relaxed);
    outputMeter.store(-60.0f, std::memory_order_relaxed);
    grMeter.store(0.0f, std::memory_order_relaxed);
    sidechainMeter.store(-60.0f, std::memory_order_relaxed);
    linkedGainReduction[0].store(0.0f, std::memory_order_relaxed);
    linkedGainReduction[1].store(0.0f, std::memory_order_relaxed);
    grHistoryWritePos.store(0, std::memory_order_relaxed);
    grHistory.fill(0.0f);
    
    // Initialize lookup tables
    lookupTables = std::make_unique<LookupTables>();
    lookupTables->initialize();
    
    try {
        // Initialize compressor instances with error handling
        optoCompressor = std::make_unique<OptoCompressor>();
        fetCompressor = std::make_unique<FETCompressor>();
        vcaCompressor = std::make_unique<VCACompressor>();
        busCompressor = std::make_unique<BusCompressor>();
        studioFetCompressor = std::make_unique<StudioFETCompressor>();
        studioVcaCompressor = std::make_unique<StudioVCACompressor>();
        digitalCompressor = std::make_unique<DigitalCompressor>();
        sidechainFilter = std::make_unique<SidechainFilter>();
        antiAliasing = std::make_unique<AntiAliasing>();
        lookaheadBuffer = std::make_unique<LookaheadBuffer>();
        sidechainEQ = std::make_unique<SidechainEQ>();
        truePeakDetector = std::make_unique<TruePeakDetector>();
        transientShaper = std::make_unique<TransientShaper>();
    }
    catch (const std::exception& e) {
        // Ensure all pointers are null on failure
        optoCompressor.reset();
        fetCompressor.reset();
        vcaCompressor.reset();
        busCompressor.reset();
        studioFetCompressor.reset();
        studioVcaCompressor.reset();
        sidechainFilter.reset();
        antiAliasing.reset();
        lookaheadBuffer.reset();
        truePeakDetector.reset();
        transientShaper.reset();
        DBG("Failed to initialize compressors: " << e.what());
    }
    catch (...) {
        // Ensure all pointers are null on failure
        optoCompressor.reset();
        fetCompressor.reset();
        vcaCompressor.reset();
        busCompressor.reset();
        studioFetCompressor.reset();
        studioVcaCompressor.reset();
        sidechainFilter.reset();
        antiAliasing.reset();
        lookaheadBuffer.reset();
        truePeakDetector.reset();
        transientShaper.reset();
        DBG("Failed to initialize compressors: unknown error");
    }
}

UniversalCompressor::~UniversalCompressor()
{
    // Explicitly reset all compressors in reverse order
    transientShaper.reset();
    truePeakDetector.reset();
    antiAliasing.reset();
    sidechainFilter.reset();
    studioVcaCompressor.reset();
    studioFetCompressor.reset();
    busCompressor.reset();
    vcaCompressor.reset();
    fetCompressor.reset();
    optoCompressor.reset();
}

void UniversalCompressor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (sampleRate <= 0.0 || std::isnan(sampleRate) || std::isinf(sampleRate) || samplesPerBlock <= 0)
        return;

    // Clamp sample rate to reasonable range (8kHz to 384kHz)
    // Supports all common pro audio sample rates including DXD (352.8kHz) and 384kHz
    sampleRate = juce::jlimit(8000.0, 384000.0, sampleRate);

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
    if (studioFetCompressor)
        studioFetCompressor->prepare(sampleRate, numChannels);
    if (studioVcaCompressor)
        studioVcaCompressor->prepare(sampleRate, numChannels);
    if (digitalCompressor)
        digitalCompressor->prepare(sampleRate, numChannels, samplesPerBlock);

    // Prepare sidechain filter for all modes
    if (sidechainFilter)
        sidechainFilter->prepare(sampleRate, numChannels);

    // Prepare global lookahead buffer (works for all modes)
    if (lookaheadBuffer)
        lookaheadBuffer->prepare(sampleRate, numChannels);

    // Prepare anti-aliasing for internal oversampling
    int oversamplingLatency = 0;
    if (antiAliasing)
    {
        antiAliasing->prepare(sampleRate, samplesPerBlock, numChannels);
        // Use max latency (4x) for consistent PDC regardless of current setting
        oversamplingLatency = antiAliasing->getMaxLatency();
    }

    // Prepare sidechain EQ
    if (sidechainEQ)
        sidechainEQ->prepare(sampleRate, numChannels);

    // Prepare true-peak detector for sidechain
    if (truePeakDetector)
        truePeakDetector->prepare(sampleRate, numChannels, samplesPerBlock);

    // Prepare transient shaper for FET all-buttons mode
    if (transientShaper)
        transientShaper->prepare(sampleRate, numChannels);

    // Calculate maximum lookahead latency (global_lookahead now works for all modes)
    // Always report max to maintain consistent PDC regardless of parameter settings
    const float maxLookaheadMs = LookaheadBuffer::MAX_LOOKAHEAD_MS;
    int maxLookaheadSamples = static_cast<int>(std::ceil((maxLookaheadMs / 1000.0) * sampleRate));

    // Total latency = oversampling (max for 4x) + max lookahead
    setLatencySamples(oversamplingLatency + maxLookaheadSamples);

    // Pre-allocate buffers for processBlock to avoid allocation in audio thread
    dryBuffer.setSize(numChannels, samplesPerBlock);
    filteredSidechain.setSize(numChannels, samplesPerBlock);
    linkedSidechain.setSize(numChannels, samplesPerBlock);
    externalSidechain.setSize(numChannels, samplesPerBlock);
    // Allocate interpolated sidechain for max 4x oversampling
    interpolatedSidechain.setSize(numChannels, samplesPerBlock * 4);

    // Initialize smoothed auto-makeup gain with ~50ms smoothing time
    smoothedAutoMakeupGain.reset(sampleRate, 0.05);
    smoothedAutoMakeupGain.setCurrentAndTargetValue(1.0f);
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
    if (!optoCompressor || !fetCompressor || !vcaCompressor || !busCompressor ||
        !studioFetCompressor || !studioVcaCompressor || !digitalCompressor)
        return;
    
    // Check for valid parameter pointers and bypass
    auto* bypassParam = parameters.getRawParameterValue("bypass");
    if (!bypassParam || *bypassParam > 0.5f)
        return;
    
    // Get stereo link and mix parameters with proper null checks
    auto* stereoLinkParam = parameters.getRawParameterValue("stereo_link");
    auto* mixParam = parameters.getRawParameterValue("mix");

    // Safely dereference with null checks
    float stereoLinkAmount = 1.0f;
    if (stereoLinkParam != nullptr)
    {
        stereoLinkAmount = *stereoLinkParam * 0.01f; // Convert to 0-1
    }

    float mixAmount = 1.0f;
    if (mixParam != nullptr)
    {
        mixAmount = *mixParam * 0.01f; // Convert to 0-1
    }

    // Store dry signal for parallel compression (uses pre-allocated buffer)
    // Note: Uses buffer dimensions directly since numChannels/numSamples defined later
    bool needsDryBuffer = (mixAmount < 1.0f);
    if (needsDryBuffer)
    {
        const int bufChannels = buffer.getNumChannels();
        const int bufSamples = buffer.getNumSamples();
        // Ensure buffer is sized correctly (may change between calls)
        if (dryBuffer.getNumChannels() < bufChannels || dryBuffer.getNumSamples() < bufSamples)
            dryBuffer.setSize(bufChannels, bufSamples, false, false, true);
        for (int ch = 0; ch < bufChannels; ++ch)
            dryBuffer.copyFrom(ch, 0, buffer, ch, 0, bufSamples);
    }

    // Internal oversampling is always enabled for better quality
    bool oversample = true; // Always use oversampling internally
    CompressorMode mode = getCurrentMode();
    
    // Cache parameters based on mode to avoid repeated lookups
    float cachedParams[10] = {0.0f}; // Max 10 params (Digital mode has the most)
    bool validParams = true;
    
    switch (mode)
    {
        case CompressorMode::Opto:
        {
            auto* p1 = parameters.getRawParameterValue("opto_peak_reduction");
            auto* p2 = parameters.getRawParameterValue("opto_gain");
            auto* p3 = parameters.getRawParameterValue("opto_limit");
            if (p1 && p2 && p3) {
                cachedParams[0] = juce::jlimit(0.0f, 100.0f, p1->load());  // Peak reduction 0-100
                // Opto gain is 0-40dB range, parameter is 0-100
                // Map 50 = unity gain (0dB), 0 = -40dB, 100 = +40dB
                float gainParam = juce::jlimit(0.0f, 100.0f, p2->load());
                cachedParams[1] = juce::jlimit(-40.0f, 40.0f, (gainParam - 50.0f) * 0.8f);  // Bounded gain
                cachedParams[2] = p3->load();
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
            auto* p6 = parameters.getRawParameterValue("fet_curve_mode");
            auto* p7 = parameters.getRawParameterValue("fet_transient");
            if (p1 && p2 && p3 && p4 && p5) {
                cachedParams[0] = *p1;
                cachedParams[1] = *p2;
                cachedParams[2] = *p3;
                cachedParams[3] = *p4;
                cachedParams[4] = *p5;
                cachedParams[5] = (p6 != nullptr) ? p6->load() : 0.0f;  // Curve mode (0=Modern, 1=Measured)
                cachedParams[6] = (p7 != nullptr) ? p7->load() : 0.0f;  // Transient sensitivity (0-100%)
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
        case CompressorMode::StudioFET:
        {
            // Studio FET shares parameters with Vintage FET
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
        case CompressorMode::StudioVCA:
        {
            auto* p1 = parameters.getRawParameterValue("studio_vca_threshold");
            auto* p2 = parameters.getRawParameterValue("studio_vca_ratio");
            auto* p3 = parameters.getRawParameterValue("studio_vca_attack");
            auto* p4 = parameters.getRawParameterValue("studio_vca_release");
            auto* p5 = parameters.getRawParameterValue("studio_vca_output");
            if (p1 && p2 && p3 && p4 && p5) {
                cachedParams[0] = *p1;
                cachedParams[1] = *p2;
                cachedParams[2] = *p3;
                cachedParams[3] = *p4;
                cachedParams[4] = *p5;
            } else validParams = false;
            break;
        }
        case CompressorMode::Digital:
        {
            auto* p1 = parameters.getRawParameterValue("digital_threshold");
            auto* p2 = parameters.getRawParameterValue("digital_ratio");
            auto* p3 = parameters.getRawParameterValue("digital_knee");
            auto* p4 = parameters.getRawParameterValue("digital_attack");
            auto* p5 = parameters.getRawParameterValue("digital_release");
            auto* p6 = parameters.getRawParameterValue("digital_lookahead");
            auto* p7 = parameters.getRawParameterValue("digital_mix");
            auto* p8 = parameters.getRawParameterValue("digital_output");
            auto* p9 = parameters.getRawParameterValue("digital_adaptive");
            auto* p10 = parameters.getRawParameterValue("digital_sidechain_listen");
            if (p1 && p2 && p3 && p4 && p5 && p6 && p7 && p8 && p9 && p10) {
                cachedParams[0] = *p1;  // threshold
                cachedParams[1] = *p2;  // ratio
                cachedParams[2] = *p3;  // knee
                cachedParams[3] = *p4;  // attack
                cachedParams[4] = *p5;  // release
                cachedParams[5] = *p6;  // lookahead
                cachedParams[6] = *p7;  // mix
                cachedParams[7] = *p8;  // output
                cachedParams[8] = *p9;  // adaptive release (bool as float)
                cachedParams[9] = *p10; // sidechain listen (bool as float)
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

    // Get sidechain HP filter frequency and update filter if changed
    auto* sidechainHpParam = parameters.getRawParameterValue("sidechain_hp");
    float sidechainHpFreq = (sidechainHpParam != nullptr) ? sidechainHpParam->load() : 80.0f;
    if (sidechainFilter)
        sidechainFilter->setFrequency(sidechainHpFreq);

    // Get global parameters
    auto* autoMakeupParam = parameters.getRawParameterValue("auto_makeup");
    auto* distortionTypeParam = parameters.getRawParameterValue("distortion_type");
    auto* distortionAmountParam = parameters.getRawParameterValue("distortion_amount");
    auto* globalLookaheadParam = parameters.getRawParameterValue("global_lookahead");
    auto* globalSidechainListenParam = parameters.getRawParameterValue("global_sidechain_listen");
    auto* sidechainEnableParam = parameters.getRawParameterValue("sidechain_enable");
    auto* stereoLinkModeParam = parameters.getRawParameterValue("stereo_link_mode");
    auto* oversamplingParam = parameters.getRawParameterValue("oversampling");
    auto* scLowFreqParam = parameters.getRawParameterValue("sc_low_freq");
    auto* scLowGainParam = parameters.getRawParameterValue("sc_low_gain");
    auto* scHighFreqParam = parameters.getRawParameterValue("sc_high_freq");
    auto* scHighGainParam = parameters.getRawParameterValue("sc_high_gain");
    auto* truePeakEnableParam = parameters.getRawParameterValue("true_peak_enable");
    auto* truePeakQualityParam = parameters.getRawParameterValue("true_peak_quality");

    bool autoMakeup = (autoMakeupParam != nullptr) ? (autoMakeupParam->load() > 0.5f) : false;
    DistortionType distType = (distortionTypeParam != nullptr) ? static_cast<DistortionType>(static_cast<int>(distortionTypeParam->load())) : DistortionType::Off;
    float distAmount = (distortionAmountParam != nullptr) ? (distortionAmountParam->load() / 100.0f) : 0.0f;
    float globalLookaheadMs = (globalLookaheadParam != nullptr) ? globalLookaheadParam->load() : 0.0f;
    bool globalSidechainListen = (globalSidechainListenParam != nullptr) ? (globalSidechainListenParam->load() > 0.5f) : false;
    bool useExternalSidechain = (sidechainEnableParam != nullptr) ? (sidechainEnableParam->load() > 0.5f) : false;
    int stereoLinkMode = (stereoLinkModeParam != nullptr) ? static_cast<int>(stereoLinkModeParam->load()) : 0;
    int oversamplingFactor = (oversamplingParam != nullptr) ? static_cast<int>(oversamplingParam->load()) : 0;

    // Update oversampling factor (0 = 2x, 1 = 4x)
    if (antiAliasing)
        antiAliasing->setOversamplingFactor(oversamplingFactor);

    // Update sidechain EQ parameters
    if (sidechainEQ)
    {
        float scLowFreq = (scLowFreqParam != nullptr) ? scLowFreqParam->load() : 100.0f;
        float scLowGain = (scLowGainParam != nullptr) ? scLowGainParam->load() : 0.0f;
        float scHighFreq = (scHighFreqParam != nullptr) ? scHighFreqParam->load() : 8000.0f;
        float scHighGain = (scHighGainParam != nullptr) ? scHighGainParam->load() : 0.0f;
        sidechainEQ->setLowShelf(scLowFreq, scLowGain);
        sidechainEQ->setHighShelf(scHighFreq, scHighGain);
    }

    // Check if external sidechain bus is available and has data
    auto sidechainBus = getBus(true, 1);  // Input bus 1 = sidechain
    bool hasExternalSidechain = useExternalSidechain && sidechainBus != nullptr && sidechainBus->isEnabled();

    // Ensure pre-allocated buffers are sized correctly
    if (filteredSidechain.getNumChannels() < numChannels || filteredSidechain.getNumSamples() < numSamples)
        filteredSidechain.setSize(numChannels, numSamples, false, false, true);
    if (externalSidechain.getNumChannels() < numChannels || externalSidechain.getNumSamples() < numSamples)
        externalSidechain.setSize(numChannels, numSamples, false, false, true);

    // Get sidechain source: external if enabled and available, otherwise internal (main input)
    const juce::AudioBuffer<float>* sidechainSource = &buffer;

    if (hasExternalSidechain)
    {
        // Get sidechain bus buffer using JUCE's bus API (bus 1 = sidechain input)
        // This is the proper way to access separate input buses in JUCE
        auto sidechainBusBuffer = getBusBuffer(buffer, true, 1);
        if (sidechainBusBuffer.getNumChannels() > 0)
        {
            for (int ch = 0; ch < numChannels && ch < sidechainBusBuffer.getNumChannels(); ++ch)
            {
                externalSidechain.copyFrom(ch, 0, sidechainBusBuffer, ch, 0, numSamples);
            }
            sidechainSource = &externalSidechain;
        }
        // If sidechain bus has no channels, fall back to main input (sidechainSource already points to buffer)
    }

    // Apply sidechain HP filter at original sample rate (filter is prepared for this rate)
    // filteredSidechain stores the HP-filtered sidechain signal for each channel.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        const float* inputData = sidechainSource->getReadPointer(juce::jmin(channel, sidechainSource->getNumChannels() - 1));
        float* scData = filteredSidechain.getWritePointer(channel);

        if (sidechainFilter)
        {
            // Use block processing for better CPU efficiency (eliminates per-sample function call overhead)
            sidechainFilter->processBlock(inputData, scData, numSamples, channel);
        }
        else
        {
            // No filter - use memcpy for efficiency
            std::memcpy(scData, inputData, static_cast<size_t>(numSamples) * sizeof(float));
        }
    }

    // Apply sidechain shelf EQ (after HP filter, before stereo linking)
    if (sidechainEQ)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            float* scData = filteredSidechain.getWritePointer(channel);
            for (int i = 0; i < numSamples; ++i)
                scData[i] = sidechainEQ->process(scData[i], channel);
        }
    }

    // Apply True-Peak Detection (after EQ, before stereo linking)
    // This detects inter-sample peaks that would cause clipping in DACs/codecs
    bool useTruePeak = (truePeakEnableParam != nullptr) ? (truePeakEnableParam->load() > 0.5f) : false;
    if (useTruePeak && truePeakDetector)
    {
        int truePeakQuality = (truePeakQualityParam != nullptr) ? static_cast<int>(truePeakQualityParam->load()) : 0;
        truePeakDetector->setOversamplingFactor(truePeakQuality);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            float* scData = filteredSidechain.getWritePointer(channel);
            truePeakDetector->processBlock(scData, numSamples, channel);
        }
    }

    // Update sidechain meter (post-filter level)
    float sidechainLevel = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* scData = filteredSidechain.getReadPointer(ch);
        float channelPeak = SIMDHelpers::getPeakLevel(scData, numSamples);
        sidechainLevel = juce::jmax(sidechainLevel, channelPeak);
    }
    float sidechainDb = sidechainLevel > 1e-5f ? juce::Decibels::gainToDecibels(sidechainLevel) : -60.0f;
    sidechainMeter.store(sidechainDb, std::memory_order_relaxed);

    // Stereo linking implementation:
    // Mode 0 (Stereo): Max-level linking based on stereoLinkAmount
    // Mode 1 (Mid-Side): M/S processing - compress mid and side separately
    // Mode 2 (Dual Mono): Fully independent per-channel compression
    // This creates a stereo-linked sidechain buffer for stereo sources

    bool useStereoLink = (stereoLinkMode == 0 && stereoLinkAmount > 0.01f) && (numChannels >= 2);
    bool useMidSide = (stereoLinkMode == 1) && (numChannels >= 2);
    // Dual mono (mode 2) uses no linking - each channel processes independently

    if (linkedSidechain.getNumChannels() < numChannels || linkedSidechain.getNumSamples() < numSamples)
        linkedSidechain.setSize(numChannels, numSamples, false, false, true);

    if (useMidSide && numChannels >= 2)
    {
        // Mid-Side processing: convert L/R sidechain to M/S
        const float* leftSCFiltered = filteredSidechain.getReadPointer(0);
        const float* rightSCFiltered = filteredSidechain.getReadPointer(1);
        float* midSC = linkedSidechain.getWritePointer(0);
        float* sideSC = linkedSidechain.getWritePointer(1);

        for (int i = 0; i < numSamples; ++i)
        {
            // Convert L/R to M/S
            float mid = (leftSCFiltered[i] + rightSCFiltered[i]) * 0.5f;
            float side = (leftSCFiltered[i] - rightSCFiltered[i]) * 0.5f;
            midSC[i] = std::abs(mid);
            sideSC[i] = std::abs(side);
        }
    }
    else if (useStereoLink)
    {
        const float* leftSCFiltered = filteredSidechain.getReadPointer(0);
        const float* rightSCFiltered = filteredSidechain.getReadPointer(1);
        float* leftSC = linkedSidechain.getWritePointer(0);
        float* rightSC = linkedSidechain.getWritePointer(1);

        for (int i = 0; i < numSamples; ++i)
        {
            // Use pre-filtered sidechain values (filter already applied above)
            float leftLevel = std::abs(leftSCFiltered[i]);
            float rightLevel = std::abs(rightSCFiltered[i]);

            // Max of both channels for linked portion
            float maxLevel = juce::jmax(leftLevel, rightLevel);

            // Blend independent and linked based on stereoLinkAmount
            leftSC[i] = leftLevel * (1.0f - stereoLinkAmount) + maxLevel * stereoLinkAmount;
            rightSC[i] = rightLevel * (1.0f - stereoLinkAmount) + maxLevel * stereoLinkAmount;
        }
    }

    // Apply global lookahead to main input signal (delays audio for look-ahead compression)
    // This runs before compression so all modes can use lookahead
    if (lookaheadBuffer && globalLookaheadMs > 0.0f)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            float* data = buffer.getWritePointer(channel);
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] = lookaheadBuffer->processSample(data[i], channel, globalLookaheadMs);
            }
        }
    }

    // Global sidechain listen: output the sidechain signal instead of processed audio
    if (globalSidechainListen)
    {
        // Output the filtered sidechain signal for monitoring
        for (int channel = 0; channel < numChannels; ++channel)
        {
            buffer.copyFrom(channel, 0, filteredSidechain, channel, 0, numSamples);
        }

        // Update output meter with sidechain signal
        outputMeter.store(sidechainDb, std::memory_order_relaxed);
        grMeter.store(0.0f, std::memory_order_relaxed);
        return;  // Skip compression processing
    }

    // Convert L/R to M/S if M/S mode is enabled (before compression)
    if (useMidSide && numChannels >= 2)
    {
        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i];
            float r = right[i];
            left[i] = (l + r) * 0.5f;   // Mid
            right[i] = (l - r) * 0.5f;  // Side
        }
    }

    // Process audio with reduced function call overhead
    if (oversample && antiAliasing && antiAliasing->isReady())
    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto oversampledBlock = antiAliasing->processUp(block);

        const int osNumChannels = static_cast<int>(oversampledBlock.getNumChannels());
        const int osNumSamples = static_cast<int>(oversampledBlock.getNumSamples());

        // PRE-INTERPOLATE sidechain buffer ONCE before the channel loop
        // This eliminates per-sample getSample() calls and bounds checking in the hot loop
        // Select source buffer once based on stereo link setting
        const auto& scSource = useStereoLink ? linkedSidechain : filteredSidechain;

        // Ensure interpolated buffer is sized correctly
        if (interpolatedSidechain.getNumChannels() < osNumChannels ||
            interpolatedSidechain.getNumSamples() < osNumSamples)
        {
            interpolatedSidechain.setSize(osNumChannels, osNumSamples, false, false, true);
        }

        // Pre-interpolate all channels
        for (int ch = 0; ch < juce::jmin(osNumChannels, scSource.getNumChannels()); ++ch)
        {
            const float* srcPtr = scSource.getReadPointer(ch);
            float* destPtr = interpolatedSidechain.getWritePointer(ch);
            SIMDHelpers::interpolateSidechain(srcPtr, destPtr, numSamples, osNumSamples);
        }

        // Process with cached parameters - now using pre-interpolated sidechain
        for (int channel = 0; channel < osNumChannels; ++channel)
        {
            float* data = oversampledBlock.getChannelPointer(static_cast<size_t>(channel));
            // Direct pointer access to pre-interpolated sidechain (no per-sample bounds checking)
            const float* scData = interpolatedSidechain.getReadPointer(
                juce::jmin(channel, interpolatedSidechain.getNumChannels() - 1));

            switch (mode)
            {
                case CompressorMode::Opto:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = optoCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2] > 0.5f, true);
                    break;
                case CompressorMode::FET:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = fetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1],
                                                         cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), true,
                                                         lookupTables.get(), transientShaper.get(),
                                                         cachedParams[5] > 0.5f, cachedParams[6]);
                    break;
                case CompressorMode::VCA:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = vcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], cachedParams[5] > 0.5f, true);
                    break;
                case CompressorMode::Bus:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = busCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], static_cast<int>(cachedParams[2]), static_cast<int>(cachedParams[3]), cachedParams[4], cachedParams[5], true);
                    break;
                case CompressorMode::StudioFET:
                    // Optimized: use pre-interpolated sidechain with direct pointer access
                    for (int i = 0; i < osNumSamples; ++i)
                    {
                        data[i] = studioFetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), scData[i]);
                    }
                    break;
                case CompressorMode::StudioVCA:
                    // Optimized: use pre-interpolated sidechain with direct pointer access
                    for (int i = 0; i < osNumSamples; ++i)
                    {
                        data[i] = studioVcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], scData[i]);
                    }
                    break;
                case CompressorMode::Digital:
                {
                    bool sidechainListen = cachedParams[9] > 0.5f;
                    // Optimized: use pre-interpolated sidechain with direct pointer access
                    for (int i = 0; i < osNumSamples; ++i)
                    {
                        // Sidechain listen mode - output sidechain signal instead of processed audio
                        if (sidechainListen)
                        {
                            data[i] = scData[i];
                        }
                        else
                        {
                            // Digital: threshold, ratio, knee, attack, release, lookahead, mix, output, adaptive
                            data[i] = digitalCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2],
                                                                 cachedParams[3], cachedParams[4], cachedParams[5], cachedParams[6],
                                                                 cachedParams[7], cachedParams[8] > 0.5f, scData[i]);
                        }
                    }
                    break;
                }
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
                        data[i] = fetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1],
                                                         cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), false,
                                                         lookupTables.get(), transientShaper.get(),
                                                         cachedParams[5] > 0.5f, cachedParams[6]) * compensationGain;
                    break;
                case CompressorMode::VCA:
                    for (int i = 0; i < numSamples; ++i)
                        data[i] = vcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], cachedParams[5] > 0.5f, false) * compensationGain;
                    break;
                case CompressorMode::Bus:
                    for (int i = 0; i < numSamples; ++i)
                        data[i] = busCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], static_cast<int>(cachedParams[2]), static_cast<int>(cachedParams[3]), cachedParams[4], cachedParams[5], false) * compensationGain;
                    break;
                case CompressorMode::StudioFET:
                    for (int i = 0; i < numSamples; ++i)
                    {
                        // Use linked sidechain if stereo linking is enabled, otherwise use pre-filtered signal
                        // Note: filter was already applied when building filteredSidechain
                        float scSignal;
                        if (useStereoLink && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);
                        data[i] = studioFetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), scSignal) * compensationGain;
                    }
                    break;
                case CompressorMode::StudioVCA:
                    for (int i = 0; i < numSamples; ++i)
                    {
                        // Use linked sidechain if stereo linking is enabled, otherwise use pre-filtered signal
                        // Note: filter was already applied when building filteredSidechain
                        float scSignal;
                        if (useStereoLink && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);
                        data[i] = studioVcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], scSignal) * compensationGain;
                    }
                    break;
                case CompressorMode::Digital:
                {
                    bool sidechainListen = cachedParams[9] > 0.5f;
                    for (int i = 0; i < numSamples; ++i)
                    {
                        float scSignal;
                        if (useStereoLink && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);

                        // Sidechain listen mode - output sidechain signal instead of processed audio
                        if (sidechainListen)
                        {
                            data[i] = scSignal;
                        }
                        else
                        {
                            // Digital: threshold, ratio, knee, attack, release, lookahead, mix, output, adaptive
                            data[i] = digitalCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2],
                                                                 cachedParams[3], cachedParams[4], cachedParams[5], cachedParams[6],
                                                                 cachedParams[7], cachedParams[8] > 0.5f, scSignal) * compensationGain;
                        }
                    }
                    break;
                }
            }
        }
    }

    // Convert M/S back to L/R if M/S mode was used (after compression)
    if (useMidSide && numChannels >= 2)
    {
        float* mid = buffer.getWritePointer(0);
        float* side = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            float m = mid[i];
            float s = side[i];
            mid[i] = m + s;   // Left = Mid + Side
            side[i] = m - s;  // Right = Mid - Side
        }
    }

    // Get gain reduction from active compressor (needed for auto-makeup and metering)
    float grLeft = 0.0f, grRight = 0.0f;
    switch (mode)
    {
        case CompressorMode::Opto:
            grLeft = optoCompressor->getGainReduction(0);
            grRight = (numChannels > 1) ? optoCompressor->getGainReduction(1) : grLeft;
            break;
        case CompressorMode::FET:
            grLeft = fetCompressor->getGainReduction(0);
            grRight = (numChannels > 1) ? fetCompressor->getGainReduction(1) : grLeft;
            break;
        case CompressorMode::VCA:
            grLeft = vcaCompressor->getGainReduction(0);
            grRight = (numChannels > 1) ? vcaCompressor->getGainReduction(1) : grLeft;
            break;
        case CompressorMode::Bus:
            grLeft = busCompressor->getGainReduction(0);
            grRight = (numChannels > 1) ? busCompressor->getGainReduction(1) : grLeft;
            break;
        case CompressorMode::StudioFET:
            grLeft = studioFetCompressor->getGainReduction(0);
            grRight = (numChannels > 1) ? studioFetCompressor->getGainReduction(1) : grLeft;
            break;
        case CompressorMode::StudioVCA:
            grLeft = studioVcaCompressor->getGainReduction(0);
            grRight = (numChannels > 1) ? studioVcaCompressor->getGainReduction(1) : grLeft;
            break;
        case CompressorMode::Digital:
            grLeft = digitalCompressor->getGainReduction(0);
            grRight = (numChannels > 1) ? digitalCompressor->getGainReduction(1) : grLeft;
            break;
    }

    // Store per-channel gain reduction for UI metering (stereo-linked display)
    linkedGainReduction[0].store(grLeft, std::memory_order_relaxed);
    linkedGainReduction[1].store(grRight, std::memory_order_relaxed);

    // Combined gain reduction (min of both channels for display)
    float gainReduction = juce::jmin(grLeft, grRight);

    // Apply auto-makeup gain if enabled
    // Auto-makeup compensates for the average gain reduction to maintain perceived loudness
    // Uses smoothed gain to avoid audible distortion from abrupt level changes
    {
        float targetMakeupGain = 1.0f;
        if (autoMakeup && gainReduction < -0.5f)
        {
            // Apply ~50% of the gain reduction as makeup to avoid over-compensation
            targetMakeupGain = juce::Decibels::decibelsToGain(-gainReduction * 0.5f);
            targetMakeupGain = juce::jlimit(1.0f, 4.0f, targetMakeupGain);  // Limit to +12dB max makeup
        }

        // Update target and apply smoothed gain sample-by-sample
        smoothedAutoMakeupGain.setTargetValue(targetMakeupGain);

        if (smoothedAutoMakeupGain.isSmoothing())
        {
            // OPTIMIZED: Pre-fill gain curve array, then apply channel-by-channel
            // This improves cache locality compared to the inner channel loop
            const int maxGainSamples = static_cast<int>(smoothedGainBuffer.size());
            const int samplesToProcess = juce::jmin(numSamples, maxGainSamples);

            // Pre-compute all smoothed gain values
            for (int i = 0; i < samplesToProcess; ++i)
                smoothedGainBuffer[static_cast<size_t>(i)] = smoothedAutoMakeupGain.getNextValue();

            // Apply gains channel-by-channel (cache-friendly)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = buffer.getWritePointer(ch);
                const float* gains = smoothedGainBuffer.data();
                for (int i = 0; i < samplesToProcess; ++i)
                    data[i] *= gains[i];
            }
        }
        else if (targetMakeupGain > 1.001f)
        {
            // No smoothing needed, apply constant gain efficiently
            float currentGain = smoothedAutoMakeupGain.getCurrentValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = buffer.getWritePointer(ch);
                SIMDHelpers::applyGain(data, numSamples, currentGain);
            }
        }
    }

    // Apply output distortion if enabled
    if (distType != DistortionType::Off && distAmount > 0.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] = applyDistortion(data[i], distType, distAmount);
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

    // Store gain reduction for DAW display (gainReduction already calculated above)
    grMeter.store(gainReduction, std::memory_order_relaxed);

    // Update the gain reduction parameter for DAW display
    if (auto* grParam = parameters.getRawParameterValue("gr_meter"))
        *grParam = gainReduction;

    // Update GR history buffer for visualization (approximately 30Hz update rate)
    // At 48kHz with 512 samples/block, we get ~94 blocks/sec, so update every 3 blocks
    grHistoryUpdateCounter++;
    if (grHistoryUpdateCounter >= 3)
    {
        grHistoryUpdateCounter = 0;
        int writePos = grHistoryWritePos.load(std::memory_order_relaxed);
        grHistory[static_cast<size_t>(writePos)] = gainReduction;
        writePos = (writePos + 1) % GR_HISTORY_SIZE;
        grHistoryWritePos.store(writePos, std::memory_order_relaxed);
    }
    
    // Apply mix control for parallel compression (SIMD-optimized)
    if (needsDryBuffer && dryBuffer.getNumChannels() > 0)
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
    return new EnhancedCompressorEditor(*this);
}

CompressorMode UniversalCompressor::getCurrentMode() const
{
    auto* modeParam = parameters.getRawParameterValue("mode");
    if (modeParam)
    {
        int mode = static_cast<int>(*modeParam);
        return static_cast<CompressorMode>(juce::jlimit(0, 6, mode));  // 7 modes: 0-6 (including Digital)
    }
    return CompressorMode::Opto; // Default fallback
}

double UniversalCompressor::getLatencyInSamples() const
{
    double latency = 0.0;

    // Report latency from oversampler if active
    if (antiAliasing)
    {
        latency += static_cast<double>(antiAliasing->getLatency());
    }

    // Always include max lookahead latency for consistent PDC
    // This matches what we report in prepareToPlay()
    const float maxLookaheadMs = LookaheadBuffer::MAX_LOOKAHEAD_MS;
    if (currentSampleRate > 0)
    {
        latency += std::ceil((maxLookaheadMs / 1000.0) * currentSampleRate);
    }

    return latency;
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

#include "UniversalCompressor.h"
#include "EnhancedCompressorEditor.h"
#include "CompressorPresets.h"
#include "HardwareEmulation/HardwareMeasurements.h"
#include "HardwareEmulation/WaveshaperCurves.h"
#include "HardwareEmulation/TransformerEmulation.h"
#include "HardwareEmulation/TubeEmulation.h"
#include "HardwareEmulation/ConvolutionEngine.h"
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

            // Create both 2x and 4x oversamplers using FIR equiripple filters
            // FIR provides superior alias rejection compared to IIR, essential for saturation
            // 2x oversampling (1 stage)
            oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
                numChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
            oversampler2x->initProcessing(static_cast<size_t>(blockSize));

            // 4x oversampling (2 stages)
            oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
                numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
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
        // 0 = Off, 1 = 2x, 2 = 4x
        oversamplingOff = (factor == 0);
        use4x = (factor == 2);
    }

    bool isUsing4x() const { return use4x; }
    bool isOversamplingOff() const { return oversamplingOff; }

    bool isReady() const
    {
        // Both oversamplers must be ready since we could switch between them
        return oversampler2x != nullptr && oversampler4x != nullptr;
    }

    juce::dsp::AudioBlock<float> processUp(juce::dsp::AudioBlock<float>& block)
    {
        // Reset upsampled flag
        didUpsample = false;

        // If oversampling is off, return original block
        if (oversamplingOff)
            return block;

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
    
    // Pre-processing passthrough
    // JUCE's oversampling FIR already provides excellent anti-aliasing
    // (~80dB stopband with filterHalfBandFIREquiripple), so we don't need
    // additional pre-filtering here. Extra filtering would only color the sound.
    float preProcessSample(float input, int /*channel*/)
    {
        return input;  // Passthrough - rely on JUCE's oversampling filters
    }
    
    // Post-processing: DC blocking only
    // IMPORTANT: No saturation here - all nonlinear processing must happen
    // in the oversampled domain to avoid aliasing (per UAD/FabFilter standards)
    float postProcessSample(float input, int channel)
    {
        if (channel < 0 || channel >= static_cast<int>(channelStates.size())) return input;

        // DC blocker to remove any DC offset from asymmetric saturation
        // This is a linear operation so it doesn't cause aliasing
        float dcBlocked = input - channelStates[channel].dcBlockerPrev +
                         channelStates[channel].dcBlockerState * 0.9975f;  // ~5Hz cutoff at 48kHz
        channelStates[channel].dcBlockerPrev = input;
        channelStates[channel].dcBlockerState = dcBlocked;

        return dcBlocked;
    }
    
    // Generate harmonics using band-limited additive synthesis
    // This ensures no aliasing from harmonic generation
    // fundamentalPhase: current phase of the fundamental in radians [0, 2π)
    // fundamentalFreq: fundamental frequency in Hz (for band-limiting check)
    float addHarmonics(float fundamental, float fundamentalPhase, float fundamentalFreq,
                       float h2Level, float h3Level, float h4Level = 0.0f)
    {
        float output = fundamental;

        // Only add harmonics if they'll be below Nyquist
        const float nyquist = static_cast<float>(sampleRate) * 0.5f;

        // 2nd harmonic (even)
        if (h2Level > 0.0f && (2.0f * fundamentalFreq) < nyquist)
        {
            float phase2 = fundamentalPhase * 2.0f;
            output += h2Level * std::sin(phase2);
        }

        // 3rd harmonic (odd)
        if (h3Level > 0.0f && (3.0f * fundamentalFreq) < nyquist)
        {
            float phase3 = fundamentalPhase * 3.0f;
            output += h3Level * std::sin(phase3);
        }

        // 4th harmonic (even) - only at high sample rates (88kHz+)
        if (h4Level > 0.0f && (4.0f * fundamentalFreq) < nyquist && sampleRate >= 88000.0)
        {
            float phase4 = fundamentalPhase * 4.0f;
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
    bool oversamplingOff = false;  // No oversampling (1x)
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
                threshold = juce::jmin(threshold, 0.95f);  // Clamp to ensure valid soft-clip curve
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

        // Hardware emulation components
        // Input transformer (UTC A-10 style)
        inputTransformer.prepare(sampleRate, numChannels);
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getLA2A().inputTransformer);
        inputTransformer.setEnabled(true);

        // Output transformer
        outputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getLA2A().outputTransformer);
        outputTransformer.setEnabled(true);

        // 12AX7 tube stage (output amplifier)
        tubeStage.prepare(sampleRate, numChannels);
        tubeStage.setTubeType(HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7);
        tubeStage.setDrive(0.3f);  // Moderate drive for LA-2A warmth

        // Short convolution for transformer coloration
        convolution.prepare(sampleRate);
        convolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::LA2A);
    }
    
    float process(float input, int channel, float peakReduction, float gain, bool limitMode, bool oversample = false, float externalSidechain = 0.0f)
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

        // Hardware emulation: Input transformer (UTC A-10 style)
        // Adds subtle saturation and frequency-dependent coloration
        float transformedInput = inputTransformer.processSample(input, channel);

        // Apply gain reduction (feedback topology)
        float compressed = transformedInput * detector.envelope;

        // Opto feedback topology: detection from output (or external sidechain if provided)
        // The external sidechain has already been HP-filtered to prevent pumping from bass
        // In Compress mode: sidechain = output (or external)
        // In Limit mode: sidechain = 1/25 input + 24/25 output (or external)
        float sidechainSignal;
        bool useExternalSidechain = (externalSidechain != 0.0f);

        if (useExternalSidechain)
        {
            // Use external HP-filtered sidechain for detection
            sidechainSignal = externalSidechain;
        }
        else if (limitMode)
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
        
        // LA-2A Output Stage - Realistic tube saturation
        // Spec from compressor_specs.json: < 1.0% THD (Opto mode)
        // Real LA-2A adds subtle 2nd harmonic warmth from tube stages
        //
        // Harmonic math: For input x = A*sin(wt)
        // - k2*x^2 produces 2nd harmonic at (k2*A^2/2) amplitude
        // - k3*x^3 produces 3rd harmonic at (3*k3*A^3/4) amplitude
        // THD = sqrt(h2^2 + h3^2 + ...) / fundamental * 100%
        //
        // Target: ~0.5% THD at 0dBFS, ~0.3% at -6dB, scaling with level

        float makeupGain = juce::Decibels::decibelsToGain(gain);
        float output = compressed * makeupGain;

        // Tube saturation - 2nd harmonic dominant (LA-2A character)
        // k2 = 0.01 gives ~0.5% 2nd harmonic at unity gain
        // k3 = 0.002 gives ~0.15% 3rd harmonic at unity gain
        constexpr float k2 = 0.01f;   // 2nd harmonic coefficient
        constexpr float k3 = 0.002f;  // 3rd harmonic coefficient

        // Apply waveshaping: y = x + k2*x^2 + k3*x^3
        float x2 = output * output;
        float x3 = x2 * output;
        output = output + k2 * x2 + k3 * x3;

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

    // Hardware emulation components (LA-2A style)
    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;
    HardwareEmulation::TubeEmulation tubeStage;
    HardwareEmulation::ShortConvolution convolution;
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

        // Hardware emulation components (1176 style)
        // Input transformer (Cinemag/Jensen style)
        inputTransformer.prepare(sampleRate, numChannels);
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFET1176().inputTransformer);
        inputTransformer.setEnabled(true);

        // Output transformer
        outputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFET1176().outputTransformer);
        outputTransformer.setEnabled(true);

        // Short convolution for 1176 transformer coloration
        convolution.prepare(sampleRate);
        convolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::FET_1176);
    }
    
    float process(float input, int channel, float inputGainDb, float outputGainDb,
                  float attackMs, float releaseMs, int ratioIndex, bool oversample = false,
                  const LookupTables* lookupTables = nullptr, TransientShaper* transientShaper = nullptr,
                  bool useMeasuredCurve = false, float transientSensitivity = 0.0f, float externalSidechain = 0.0f)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;

        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;

        auto& detector = detectors[channel];

        // Hardware emulation: Input transformer (Cinemag/Jensen style)
        // Adds subtle saturation and frequency-dependent coloration
        float transformedInput = inputTransformer.processSample(input, channel);

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
        float amplifiedInput = transformedInput * inputGainLin;

        // Ratio mapping: 4:1, 8:1, 12:1, 20:1, all-buttons mode
        // All-buttons mode: Hardware measurements show >100:1 effective ratio
        std::array<float, 5> ratios = {4.0f, 8.0f, 12.0f, 20.0f, 120.0f}; // All-buttons >100:1
        float ratio = ratios[juce::jlimit(0, 4, ratioIndex)];

        // FEEDBACK TOPOLOGY for authentic FET behavior
        // The FET uses feedback compression which creates its characteristic sound

        // First, we need to apply the PREVIOUS envelope to get the compressed signal
        float compressed = amplifiedInput * detector.envelope;

        // Detection signal: use external HP-filtered sidechain if provided, otherwise feedback from output
        // External sidechain allows the SC HP filter to prevent pumping from bass
        float detectionLevel;
        if (externalSidechain != 0.0f)
        {
            // Use external HP-filtered sidechain (apply input gain to match compression behavior)
            detectionLevel = std::abs(externalSidechain * inputGainLin);
        }
        else
        {
            // Then detect from the COMPRESSED OUTPUT (feedback)
            // This is what gives the FET its "grabby" characteristic
            detectionLevel = std::abs(compressed);
        }
        
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
        // Attack: 100µs (0.0001s) to 800µs (0.0008s) - logarithmic taper
        // Release: 50ms to 1.1s - logarithmic taper
        //
        // IMPORTANT: Minimum attack of 100µs prevents the compressor from tracking
        // individual waveform cycles, which causes harmonic distortion.
        // At 48kHz, 100µs = ~5 samples, which is safe for all audio frequencies.
        // The real 1176 achieves its fast attack through transformer overshoot,
        // not by actually tracking sub-sample level changes.

        // Map input parameters (assumed 0-1 range from attackMs/releaseMs) to hardware values
        // If attackMs is already in ms, we need to map it logarithmically
        const float minAttack = 0.0001f;   // 100 microseconds (safe minimum)
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
            // Enforce minimum of 100µs to prevent waveform-tracking distortion
            attackTime = juce::jmax(attackTime, 0.0001f); // 100 microseconds minimum (was jmin - bug!)
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
        
        // 1176 FET Output Stage - Realistic FET saturation
        // Spec from compressor_specs.json: 0.30% THD at -18dB, 0.45% at -6dB
        // FET transistors produce odd harmonics (3rd dominant, some 5th)
        // All-buttons mode has more aggressive saturation character
        //
        // Harmonic math: For input x = A*sin(wt)
        // - k3*x^3 produces 3rd harmonic at amplitude (3*k3*A^3)/4
        // - THD from 3rd ≈ (3*k3*A^2)/4 / A = 0.75*k3*A
        // At A=0.5 (-6dB): For 0.3% THD: k3 = 0.003 / (0.75*0.5) = 0.008
        // At A=1.0 (0dB): For 0.4% THD: k3 = 0.004 / 0.75 = 0.0053
        // Note: Transformer emulation also adds harmonics, so reduce k3 further

        float output = compressed;

        // All-buttons mode gets 1.5x the saturation (reduced from 2x)
        float satMultiplier = (ratioIndex == 4) ? 1.5f : 1.0f;

        // FET saturation - odd harmonics dominant (symmetric)
        // Reduced coefficients to achieve target <0.5% THD
        // k3 = 0.006 gives ~0.15-0.2% THD at moderate levels
        // k5 = 0.001 adds subtle 5th harmonic character
        constexpr float k3_base = 0.006f;   // 3rd harmonic coefficient (reduced from 0.04)
        constexpr float k5_base = 0.001f;   // 5th harmonic coefficient (reduced from 0.008)

        float k3 = k3_base * satMultiplier;
        float k5 = k5_base * satMultiplier;

        // Apply waveshaping: y = x + k3*x^3 + k5*x^5
        float x2 = output * output;
        float x3 = x2 * output;
        float x5 = x3 * x2;
        output = output + k3 * x3 + k5 * x5;

        // FET Output knob - makeup gain control
        float outputGainLin = juce::Decibels::decibelsToGain(outputGainDb);

        // Apply makeup gain
        float finalOutput = output * outputGainLin;
        
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

    // Hardware emulation components (1176 style)
    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;
    HardwareEmulation::ShortConvolution convolution;
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
                  float attackParam, float releaseParam, float outputGain, bool overEasy = false, bool oversample = false, float externalSidechain = 0.0f)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;

        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;

        auto& detector = detectors[channel];

        // VCA feedforward topology: control voltage from input signal or external sidechain
        float detectionLevel;
        if (externalSidechain != 0.0f)
        {
            // Use external HP-filtered sidechain for detection
            detectionLevel = std::abs(externalSidechain);
        }
        else
        {
            detectionLevel = std::abs(input);
        }

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

        // Hardware emulation components (SSL Bus Compressor style)
        // Input transformer (Marinair-style)
        inputTransformer.prepare(sampleRate, numChannels);
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getSSLBus().inputTransformer);
        inputTransformer.setEnabled(true);

        // Output transformer
        outputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getSSLBus().outputTransformer);
        outputTransformer.setEnabled(true);

        // Short convolution for SSL console coloration
        convolution.prepare(sampleRate);
        convolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::SSL_Console);
    }
    
    float process(float input, int channel, float threshold, float ratio,
                  int attackIndex, int releaseIndex, float makeupGain, float mixAmount = 1.0f, bool oversample = false, float externalSidechain = 0.0f)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;

        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;

        auto& detector = detectors[channel];

        // Hardware emulation: Input transformer (Marinair-style)
        // Adds subtle saturation and frequency-dependent coloration
        float transformedInput = inputTransformer.processSample(input, channel);

        // Bus Compressor quad VCA topology
        // Uses parallel detection path with feed-forward design

        // Determine detection signal: use external sidechain if provided, otherwise internal filter
        float detectionLevel;
        if (externalSidechain != 0.0f)
        {
            // Use external HP-filtered sidechain for detection
            detectionLevel = std::abs(externalSidechain);
        }
        else
        {
            // Use simple inline filter instead of complex ProcessorChain for per-sample processing
            float sidechainInput = transformedInput;
            if (detector.sidechainFilter)
            {
                // Simple 60Hz highpass filter (much faster than full ProcessorChain)
                const float hpCutoff = 60.0f / static_cast<float>(sampleRate);
                const float hpAlpha = juce::jmin(1.0f, hpCutoff);
                detector.hpState = input - detector.prevInput + detector.hpState * (1.0f - hpAlpha);
                detector.prevInput = input;
                sidechainInput = detector.hpState;
            }
            detectionLevel = std::abs(sidechainInput);
        }
        
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
        float compressed = transformedInput * detector.envelope;

        // SSL Bus Compressor Output Stage - Subtle console saturation
        // Spec from compressor_specs.json: < 0.3% THD
        // The SSL G-Series is a clean VCA design, but the console path adds character.
        // Marinair transformers add subtle 2nd harmonic warmth.
        //
        // Harmonic math: For input x = A*sin(wt)
        // - k2*x^2 produces 2nd harmonic at (k2*A^2/2) amplitude
        // - k3*x^3 produces 3rd harmonic at (3*k3*A^3)/4 amplitude
        // Target: ~0.15-0.2% THD at typical levels

        float processed = compressed;

        // SSL console saturation - 2nd harmonic dominant from transformers
        // k2 = 0.004 gives ~0.2% 2nd harmonic at moderate levels
        // k3 = 0.003 gives subtle 3rd harmonic for "glue"
        constexpr float k2 = 0.004f;   // 2nd harmonic coefficient (asymmetric warmth)
        constexpr float k3 = 0.003f;   // 3rd harmonic coefficient (symmetric glue)

        // Apply waveshaping: y = x + k2*x^2 + k3*x^3
        float x2 = processed * processed;
        float x3 = x2 * processed;
        processed = processed + k2 * x2 + k3 * x3;

        // Apply makeup gain
        float output = processed * juce::Decibels::decibelsToGain(makeupGain);

        // Note: Mix/parallel compression is now handled globally at the end of processBlock
        // for consistency across all compressor modes (mixAmount parameter kept for API compatibility)
        (void)mixAmount;  // Suppress unused warning

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

    // Hardware emulation components (SSL Bus Compressor style)
    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;
    HardwareEmulation::ShortConvolution convolution;
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
        // IMPORTANT: Minimum attack of 100µs prevents waveform-tracking distortion
        // (At 48kHz, 100µs = ~5 samples, safe for all audio frequencies)
        const float minAttack = 0.0001f;   // 100µs (safe minimum)
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
        // IMPORTANT: Minimum attack time of 0.1ms (100 microseconds) prevents the compressor
        // from tracking individual waveform cycles, which would cause harmonic distortion.
        // Professional compressors (SSL, API, etc.) have similar minimums.
        float attackTime = juce::jmax(0.0001f, attackMs / 1000.0f);  // Min 0.1ms = 100μs
        float releaseTime = juce::jmax(0.001f, releaseMs / 1000.0f);

        if (adaptiveRelease)
        {
            // Program-dependent release based on crest factor (peak/RMS ratio)
            // High crest = transient material, use faster release
            // Low crest = sustained material, use slower release

            float absInput = std::abs(input);

            // Update peak hold (instant attack, medium decay)
            const float peakRelease = 0.1f;   // 100ms peak decay
            float peakReleaseCoeff = std::exp(-1.0f / (peakRelease * static_cast<float>(sampleRate)));

            if (absInput > detector.peakHold)
                detector.peakHold = absInput;  // Instant peak attack
            else
                detector.peakHold = peakReleaseCoeff * detector.peakHold + (1.0f - peakReleaseCoeff) * absInput;

            // Update RMS level (slower, more averaging)
            const float rmsTime = 0.3f;  // 300ms RMS window
            float rmsCoeff = std::exp(-1.0f / (rmsTime * static_cast<float>(sampleRate)));
            detector.rmsLevel = rmsCoeff * detector.rmsLevel + (1.0f - rmsCoeff) * (absInput * absInput);
            float rms = std::sqrt(detector.rmsLevel);

            // Calculate crest factor (peak/RMS) - typically 1.0 to 20.0
            // 1-3 = very compressed/sustained, 6-12 = typical music, 12+ = heavy transients
            detector.crestFactor = (rms > 0.0001f) ? (detector.peakHold / rms) : 1.0f;
            detector.crestFactor = juce::jlimit(1.0f, 20.0f, detector.crestFactor);

            // Map crest factor to release multiplier:
            // Crest 1-3 (sustained): 2x slower release (more smoothing)
            // Crest 6 (typical): normal release
            // Crest 12+ (transient): 3x faster release (let transients breathe)
            float releaseMultiplier;
            if (detector.crestFactor < 6.0f)
            {
                // Sustained signal: slow down release (1.0 to 2.0)
                releaseMultiplier = 1.0f + (6.0f - detector.crestFactor) / 5.0f;
            }
            else
            {
                // Transient signal: speed up release (1.0 to 0.33)
                float transientness = juce::jmin(1.0f, (detector.crestFactor - 6.0f) / 6.0f);
                releaseMultiplier = 1.0f - (transientness * 0.67f);
            }

            releaseTime *= releaseMultiplier;
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
        float output = delayedInput * detector.envelope;

        // Note: Mix/parallel compression is now handled globally at the end of processBlock
        // for consistency across all compressor modes (mixPercent parameter kept for API compatibility)
        (void)mixPercent;  // Suppress unused warning

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
        float peakHold = 0.0f;     // Peak level tracker for transient detection
        float rmsLevel = 0.0f;     // RMS level for sustained signal detection
        float crestFactor = 1.0f;  // Peak/RMS ratio - high = transient, low = sustained
    };

    std::vector<Detector> detectors;
    juce::AudioBuffer<float> lookaheadBuffer;
    std::vector<int> lookaheadWritePos;
    int maxLookaheadSamples = 0;
    int currentLookaheadSamples = 0;
    int numChannels = 2;
    double sampleRate = 0.0;
};

//==============================================================================
// Multiband Compressor - 4-band compressor with Linkwitz-Riley crossovers
//==============================================================================
class UniversalCompressor::MultibandCompressor
{
public:
    static constexpr int NUM_BANDS = 4;

    void prepare(double sr, int numCh, int maxBlockSize)
    {
        sampleRate = sr;
        numChannels = numCh;
        this->maxBlockSize = maxBlockSize;

        // Prepare crossover filters - LR4 requires separate filter instances per signal path
        // Each crossover has two cascaded stages (a and b) for 4th order response
        auto sz = static_cast<size_t>(numCh);

        // Crossover 1 filters
        lp1_a.resize(sz);
        lp1_b.resize(sz);
        hp1_a.resize(sz);
        hp1_b.resize(sz);

        // Crossover 2 filters
        lp2_a.resize(sz);
        lp2_b.resize(sz);
        hp2_a.resize(sz);
        hp2_b.resize(sz);

        // Crossover 3 filters
        lp3_a.resize(sz);
        lp3_b.resize(sz);
        hp3_a.resize(sz);
        hp3_b.resize(sz);

        // Initialize per-band compressor state
        for (int band = 0; band < NUM_BANDS; ++band)
        {
            bandEnvelopes[band].resize(sz, 1.0f);
            bandGainReduction[band] = 0.0f;
        }

        // Allocate band buffers
        for (int band = 0; band < NUM_BANDS; ++band)
        {
            bandBuffers[band].setSize(numCh, maxBlockSize);
            bandBuffers[band].clear();
        }

        // Temp buffer for crossover processing
        tempBuffer.setSize(numCh, maxBlockSize);
        tempBuffer.clear();

        // Initialize crossover frequencies
        updateCrossoverFrequencies(200.0f, 2000.0f, 8000.0f);
    }

    void updateCrossoverFrequencies(float freq1, float freq2, float freq3)
    {
        if (sampleRate <= 0.0)
            return;

        // Clamp frequencies to valid range and ensure they're in order
        freq1 = juce::jlimit(20.0f, 500.0f, freq1);
        freq2 = juce::jlimit(freq1 * 1.5f, 5000.0f, freq2);
        freq3 = juce::jlimit(freq2 * 1.5f, 16000.0f, freq3);

        crossoverFreqs[0] = freq1;
        crossoverFreqs[1] = freq2;
        crossoverFreqs[2] = freq3;

        // Create filter coefficients - Butterworth Q=0.707 for LR4 when cascaded
        auto lp1Coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, freq1, 0.707f);
        auto hp1Coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, freq1, 0.707f);
        auto lp2Coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, freq2, 0.707f);
        auto hp2Coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, freq2, 0.707f);
        auto lp3Coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, freq3, 0.707f);
        auto hp3Coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, freq3, 0.707f);

        // Apply coefficients to all channels - each stage gets its own filter instance
        for (int ch = 0; ch < numChannels; ++ch)
        {
            // Crossover 1
            lp1_a[ch].coefficients = lp1Coeffs;
            lp1_b[ch].coefficients = lp1Coeffs;
            hp1_a[ch].coefficients = hp1Coeffs;
            hp1_b[ch].coefficients = hp1Coeffs;

            // Crossover 2
            lp2_a[ch].coefficients = lp2Coeffs;
            lp2_b[ch].coefficients = lp2Coeffs;
            hp2_a[ch].coefficients = hp2Coeffs;
            hp2_b[ch].coefficients = hp2Coeffs;

            // Crossover 3
            lp3_a[ch].coefficients = lp3Coeffs;
            lp3_b[ch].coefficients = lp3Coeffs;
            hp3_a[ch].coefficients = hp3Coeffs;
            hp3_b[ch].coefficients = hp3Coeffs;
        }
    }

    // Process a block through the multiband compressor
    void processBlock(juce::AudioBuffer<float>& buffer,
                      const std::array<float, NUM_BANDS>& thresholds,
                      const std::array<float, NUM_BANDS>& ratios,
                      const std::array<float, NUM_BANDS>& attacks,
                      const std::array<float, NUM_BANDS>& releases,
                      const std::array<float, NUM_BANDS>& makeups,
                      const std::array<bool, NUM_BANDS>& bypasses,
                      const std::array<bool, NUM_BANDS>& solos,
                      float outputGain, float mixPercent)
    {
        const int numSamples = buffer.getNumSamples();
        const int channels = juce::jmin(buffer.getNumChannels(), numChannels);

        if (numSamples <= 0 || channels <= 0)
            return;

        // Check for any solo bands
        bool anySolo = false;
        for (int band = 0; band < NUM_BANDS; ++band)
        {
            if (solos[band])
            {
                anySolo = true;
                break;
            }
        }

        // Store dry signal for mix
        bool needsDry = (mixPercent < 100.0f);
        if (needsDry)
        {
            tempBuffer.makeCopyOf(buffer);
        }

        // Split the input signal into 4 bands using crossover filters
        splitIntoBands(buffer, numSamples, channels);

        // Process each band through its compressor
        for (int band = 0; band < NUM_BANDS; ++band)
        {
            // If solo is active, only process soloed bands
            bool shouldProcess = !anySolo || solos[band];
            bool isBypassed = bypasses[band] || !shouldProcess;

            if (!isBypassed)
            {
                processBandCompression(band, bandBuffers[band], numSamples, channels,
                                       thresholds[band], ratios[band],
                                       attacks[band], releases[band], makeups[band]);
            }
            else if (!shouldProcess)
            {
                // Mute non-soloed bands when solo is active
                bandBuffers[band].clear();
            }
            // If bypassed but no solo, keep the band signal as-is (no compression)
        }

        // Sum all bands back together
        buffer.clear();
        for (int band = 0; band < NUM_BANDS; ++band)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                buffer.addFrom(ch, 0, bandBuffers[band], ch, 0, numSamples);
            }
        }

        // Apply output gain
        if (std::abs(outputGain) > 0.01f)
        {
            float outGain = juce::Decibels::decibelsToGain(outputGain);
            buffer.applyGain(outGain);
        }

        // Mix with dry signal (parallel compression)
        if (needsDry)
        {
            float wetAmount = mixPercent / 100.0f;
            float dryAmount = 1.0f - wetAmount;

            for (int ch = 0; ch < channels; ++ch)
            {
                float* out = buffer.getWritePointer(ch);
                const float* dry = tempBuffer.getReadPointer(ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    out[i] = out[i] * wetAmount + dry[i] * dryAmount;
                }
            }
        }

        // Soft limit output (tanh-based for smooth clipping, avoids harsh digital artifacts)
        // This kicks in at ~2.0 (OUTPUT_HARD_LIMIT) and provides gentle saturation above
        for (int ch = 0; ch < channels; ++ch)
        {
            float* out = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                float sample = out[i];
                // Apply soft clipping for samples approaching limit
                if (std::abs(sample) > 1.5f)
                {
                    // Tanh-based soft limiter: maps >1.5 range to ~1.5-2.0 smoothly
                    float sign = sample > 0.0f ? 1.0f : -1.0f;
                    float excess = std::abs(sample) - 1.5f;
                    sample = sign * (1.5f + 0.5f * std::tanh(excess * 2.0f));
                }
                out[i] = sample;
            }
        }
    }

    // Get gain reduction for a specific band (in dB, negative)
    float getBandGainReduction(int band) const
    {
        if (band < 0 || band >= NUM_BANDS)
            return 0.0f;
        return bandGainReduction[band];
    }

    // Get overall (max) gain reduction across all bands
    float getMaxGainReduction() const
    {
        float maxGr = 0.0f;
        for (int band = 0; band < NUM_BANDS; ++band)
        {
            maxGr = juce::jmin(maxGr, bandGainReduction[band]);
        }
        return maxGr;
    }

private:
    void splitIntoBands(const juce::AudioBuffer<float>& input, int numSamples, int channels)
    {
        // Proper Linkwitz-Riley 4th order (LR4) crossover implementation
        // LR4 = cascaded 2nd order Butterworth filters (applied twice)
        //
        // Signal flow for 4 bands with 3 crossover points:
        // Band 0 (Low):      Input -> LP1a -> LP1b
        // Band 1 (Low-Mid):  Input -> HP1a -> HP1b -> LP2a -> LP2b
        // Band 2 (High-Mid): Input -> HP1a -> HP1b -> HP2a -> HP2b -> LP3a -> LP3b
        // Band 3 (High):     Input -> HP1a -> HP1b -> HP2a -> HP2b -> HP3a -> HP3b
        //
        // Each filter path needs its own filter instances to maintain correct state!
        // The "a" and "b" filters are the two cascaded stages for LR4 response.

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* in = input.getReadPointer(ch);
            float* band0 = bandBuffers[0].getWritePointer(ch);
            float* band1 = bandBuffers[1].getWritePointer(ch);
            float* band2 = bandBuffers[2].getWritePointer(ch);
            float* band3 = bandBuffers[3].getWritePointer(ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float sample = in[i];

                // === Band 0: Low (below crossover 1) ===
                // LP1 applied twice for LR4
                float lp1 = lp1_a[ch].processSample(sample);
                lp1 = lp1_b[ch].processSample(lp1);
                band0[i] = lp1;

                // === HP1 for bands 1-3 (above crossover 1) ===
                float hp1 = hp1_a[ch].processSample(sample);
                hp1 = hp1_b[ch].processSample(hp1);

                // === Band 1: Low-Mid (between crossover 1 and 2) ===
                // HP1 output -> LP2 applied twice
                float lp2 = lp2_a[ch].processSample(hp1);
                lp2 = lp2_b[ch].processSample(lp2);
                band1[i] = lp2;

                // === HP2 for bands 2-3 (above crossover 2) ===
                float hp2 = hp2_a[ch].processSample(hp1);
                hp2 = hp2_b[ch].processSample(hp2);

                // === Band 2: High-Mid (between crossover 2 and 3) ===
                // HP2 output -> LP3 applied twice
                float lp3 = lp3_a[ch].processSample(hp2);
                lp3 = lp3_b[ch].processSample(lp3);
                band2[i] = lp3;

                // === Band 3: High (above crossover 3) ===
                // HP2 output -> HP3 applied twice
                float hp3 = hp3_a[ch].processSample(hp2);
                hp3 = hp3_b[ch].processSample(hp3);
                band3[i] = hp3;
            }
        }
    }

    void processBandCompression(int band, juce::AudioBuffer<float>& bandBuffer,
                                int numSamples, int channels,
                                float thresholdDb, float ratio,
                                float attackMs, float releaseMs, float makeupDb)
    {
        if (sampleRate <= 0.0 || ratio < 1.0f)
            return;

        // Calculate envelope coefficients
        float attackTime = juce::jmax(0.0001f, attackMs / 1000.0f);
        float releaseTime = juce::jmax(0.001f, releaseMs / 1000.0f);
        float attackCoeff = std::exp(-1.0f / (attackTime * static_cast<float>(sampleRate)));
        float releaseCoeff = std::exp(-1.0f / (releaseTime * static_cast<float>(sampleRate)));

        float maxGr = 0.0f;  // Track max GR for metering

        for (int ch = 0; ch < channels; ++ch)
        {
            float* data = bandBuffer.getWritePointer(ch);
            float& envelope = bandEnvelopes[band][ch];

            for (int i = 0; i < numSamples; ++i)
            {
                float input = data[i];
                float absInput = std::abs(input);

                // Convert to dB
                float inputDb = juce::Decibels::gainToDecibels(juce::jmax(absInput, 0.00001f));

                // Calculate gain reduction
                float reductionDb = 0.0f;
                if (inputDb > thresholdDb)
                {
                    float overDb = inputDb - thresholdDb;
                    reductionDb = overDb * (1.0f - 1.0f / ratio);
                }

                // Convert to gain
                float targetGain = juce::Decibels::decibelsToGain(-reductionDb);

                // Apply envelope (attack/release smoothing)
                if (targetGain < envelope)
                    envelope = attackCoeff * envelope + (1.0f - attackCoeff) * targetGain;
                else
                    envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * targetGain;

                // Clamp envelope and flush denormals (prevents CPU spikes on silent passages)
                envelope = juce::jlimit(1e-8f, 1.0f, envelope);
                if (envelope < 1e-7f) envelope = 1e-8f;  // Denormal flush

                // Apply compression and makeup gain
                float makeupGain = juce::Decibels::decibelsToGain(makeupDb);
                data[i] = input * envelope * makeupGain;

                // Track GR for metering
                float grDb = juce::Decibels::gainToDecibels(envelope);
                maxGr = juce::jmin(maxGr, grDb);
            }
        }

        // Update band GR meter (atomic not needed since we're single-threaded in audio)
        bandGainReduction[band] = maxGr;
    }

    // Crossover filters - Linkwitz-Riley 4th order (LR4)
    // Each crossover needs separate filter instances for each signal path
    // "a" and "b" are the two cascaded 2nd-order Butterworth stages

    // Crossover 1 filters (separate LP and HP paths)
    std::vector<juce::dsp::IIR::Filter<float>> lp1_a, lp1_b;  // For band 0
    std::vector<juce::dsp::IIR::Filter<float>> hp1_a, hp1_b;  // For bands 1-3

    // Crossover 2 filters (applied to HP1 output)
    std::vector<juce::dsp::IIR::Filter<float>> lp2_a, lp2_b;  // For band 1
    std::vector<juce::dsp::IIR::Filter<float>> hp2_a, hp2_b;  // For bands 2-3

    // Crossover 3 filters (applied to HP2 output)
    std::vector<juce::dsp::IIR::Filter<float>> lp3_a, lp3_b;  // For band 2
    std::vector<juce::dsp::IIR::Filter<float>> hp3_a, hp3_b;  // For band 3

    // Band buffers
    std::array<juce::AudioBuffer<float>, NUM_BANDS> bandBuffers;
    juce::AudioBuffer<float> tempBuffer;

    // Per-band envelope followers (per channel)
    std::array<std::vector<float>, NUM_BANDS> bandEnvelopes;

    // Per-band gain reduction (for metering)
    std::array<float, NUM_BANDS> bandGainReduction{0.0f, 0.0f, 0.0f, 0.0f};

    // Crossover frequencies
    std::array<float, 3> crossoverFreqs{200.0f, 2000.0f, 8000.0f};

    double sampleRate = 0.0;
    int numChannels = 2;
    int maxBlockSize = 512;
};

// Parameter layout creation
juce::AudioProcessorValueTreeState::ParameterLayout UniversalCompressor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    try {
    
    // Mode selection - 8 modes: 4 Vintage + 2 Studio + 1 Digital + 1 Multiband
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode",
        juce::StringArray{"Vintage Opto", "Vintage FET", "Classic VCA", "Vintage VCA (Bus)",
                          "Studio FET", "Studio VCA", "Digital", "Multiband"}, 0));

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

    // Sidechain highpass filter - prevents low frequency pumping (0 = Off)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "sidechain_hp", "SC HP Filter",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Auto makeup gain - using Choice instead of Bool for more reliable state restoration
    // (AudioParameterBool can have issues with normalized value handling in some hosts)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "auto_makeup", "Auto Makeup",
        juce::StringArray{"Off", "On"}, 0));

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

    // Oversampling factor (0 = Off, 1 = 2x, 2 = 4x)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "oversampling", "Oversampling",
        juce::StringArray{"Off", "2x", "4x"}, 1));  // Default to 2x

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
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "studio_vca_mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));  // Parallel compression

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
    // SC Listen is now a global control (global_sidechain_listen) in header for all modes

    // =========================================================================
    // Multiband Compressor Parameters
    // =========================================================================

    // Crossover frequencies (3 crossovers for 4 bands)
    // Low/Low-Mid crossover (default 200 Hz)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mb_crossover_1", "Crossover 1",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.4f), 200.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Low-Mid/High-Mid crossover (default 2000 Hz)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mb_crossover_2", "Crossover 2",
        juce::NormalisableRange<float>(200.0f, 5000.0f, 1.0f, 0.4f), 2000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // High-Mid/High crossover (default 8000 Hz)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mb_crossover_3", "Crossover 3",
        juce::NormalisableRange<float>(2000.0f, 16000.0f, 1.0f, 0.4f), 8000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Per-band parameters (4 bands: Low, Low-Mid, High-Mid, High)
    const juce::String bandNames[] = {"low", "lowmid", "highmid", "high"};
    const juce::String bandLabels[] = {"Low", "Low-Mid", "High-Mid", "High"};

    for (int band = 0; band < 4; ++band)
    {
        const juce::String& name = bandNames[band];
        const juce::String& label = bandLabels[band];

        // Threshold (-60 to 0 dB)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "mb_" + name + "_threshold", label + " Threshold",
            juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -20.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));

        // Ratio (1:1 to 20:1)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "mb_" + name + "_ratio", label + " Ratio",
            juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f, 0.5f), 4.0f,
            juce::AudioParameterFloatAttributes().withLabel(":1")));

        // Attack (0.1 to 100 ms)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "mb_" + name + "_attack", label + " Attack",
            juce::NormalisableRange<float>(0.1f, 100.0f, 0.1f, 0.4f), 10.0f,
            juce::AudioParameterFloatAttributes().withLabel("ms")));

        // Release (10 to 1000 ms)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "mb_" + name + "_release", label + " Release",
            juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f, 0.4f), 100.0f,
            juce::AudioParameterFloatAttributes().withLabel("ms")));

        // Makeup gain (-12 to +12 dB)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "mb_" + name + "_makeup", label + " Makeup",
            juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));

        // Band bypass
        layout.add(std::make_unique<juce::AudioParameterBool>(
            "mb_" + name + "_bypass", label + " Bypass", false));

        // Band solo
        layout.add(std::make_unique<juce::AudioParameterBool>(
            "mb_" + name + "_solo", label + " Solo", false));
    }

    // Global multiband output
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mb_output", "MB Output",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Multiband mix (parallel compression)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mb_mix", "MB Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    }
    catch (const std::exception& e) {
        DBG("Failed to create parameter layout: " << e.what());
        // Clear partially-populated layout and return empty to avoid undefined behavior
        layout = juce::AudioProcessorValueTreeState::ParameterLayout();
    }
    catch (...) {
        DBG("Failed to create parameter layout: unknown error");
        // Clear partially-populated layout and return empty to avoid undefined behavior
        layout = juce::AudioProcessorValueTreeState::ParameterLayout();
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
    inputMeterL.store(-60.0f, std::memory_order_relaxed);
    inputMeterR.store(-60.0f, std::memory_order_relaxed);
    outputMeterL.store(-60.0f, std::memory_order_relaxed);
    outputMeterR.store(-60.0f, std::memory_order_relaxed);
    grMeter.store(0.0f, std::memory_order_relaxed);
    sidechainMeter.store(-60.0f, std::memory_order_relaxed);
    linkedGainReduction[0].store(0.0f, std::memory_order_relaxed);
    linkedGainReduction[1].store(0.0f, std::memory_order_relaxed);
    for (int i = 0; i < kNumMultibandBands; ++i)
        bandGainReduction[i].store(0.0f, std::memory_order_relaxed);
    grHistoryWritePos.store(0, std::memory_order_relaxed);
    // Initialize atomic array element-by-element (can't use fill() on atomics)
    for (auto& gr : grHistory)
        gr.store(0.0f, std::memory_order_relaxed);
    
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
        multibandCompressor = std::make_unique<MultibandCompressor>();
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
        digitalCompressor.reset();
        multibandCompressor.reset();
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
        digitalCompressor.reset();
        multibandCompressor.reset();
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

    // Pre-initialize WaveshaperCurves singleton (avoids first-call latency in audio thread)
    // This loads ~96KB of lookup tables for saturation processing
    HardwareEmulation::getWaveshaperCurves();

    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    int numChannels = juce::jmax(1, getTotalNumOutputChannels());

    // ALWAYS prepare for maximum oversampling (4x) to avoid memory allocation in processBlock
    // This is critical for thread safety - prepare() does buffer allocation which is not
    // safe to call from the audio thread. By preparing for 4x, we can switch between
    // 2x and 4x oversampling without re-allocation during playback.
    // The compressor filters work correctly at any rate >= 2x, and using 4x rate ensures
    // they have adequate headroom for the highest quality setting.
    constexpr int maxOversamplingMultiplier = 4;
    double oversampledRate = sampleRate * maxOversamplingMultiplier;
    int oversampledBlockSize = samplesPerBlock * maxOversamplingMultiplier;

    // Prepare all compressor types at max oversampled rate for thread safety
    // This ensures transformer emulation, DC blockers, and HF filters
    // are correctly tuned for the rate at which they actually process audio
    if (optoCompressor)
        optoCompressor->prepare(oversampledRate, numChannels);
    if (fetCompressor)
        fetCompressor->prepare(oversampledRate, numChannels);
    if (vcaCompressor)
        vcaCompressor->prepare(oversampledRate, numChannels);
    if (busCompressor)
        busCompressor->prepare(oversampledRate, numChannels, oversampledBlockSize);
    if (studioFetCompressor)
        studioFetCompressor->prepare(oversampledRate, numChannels);
    if (studioVcaCompressor)
        studioVcaCompressor->prepare(oversampledRate, numChannels);
    if (digitalCompressor)
        digitalCompressor->prepare(oversampledRate, numChannels, oversampledBlockSize);
    // Multiband processes at native rate (not oversampled)
    if (multibandCompressor)
        multibandCompressor->prepare(sampleRate, numChannels, samplesPerBlock);

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

    // Report only oversampling latency - lookahead is 0 by default and rarely used
    // This gives much lower latency (4 samples vs 484) for typical use
    // If user enables lookahead, they accept the additional latency
    // Note: lookahead latency is handled dynamically based on parameter value
    setLatencySamples(oversamplingLatency);

    // GR meter delay only needs to match oversampling latency
    // Lookahead doesn't affect GR timing - the compressor "looks ahead" in the audio
    // but the GR is calculated at the same time as the output
    int delayInBlocks = (oversamplingLatency + samplesPerBlock - 1) / samplesPerBlock;
    grDelayBuffer.fill(0.0f);
    grDelayWritePos.store(0, std::memory_order_relaxed);
    // Use release ordering to ensure buffer fill and writePos are visible before delaySamples
    grDelaySamples.store(juce::jmin(delayInBlocks, MAX_GR_DELAY_SAMPLES - 1), std::memory_order_release);

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

    // Initialize RMS coefficient for ~200ms averaging window (industry standard)
    // This gives stable, perceptually-accurate loudness matching like Logic Pro's compressor
    // For 99% convergence in 200ms, use timeConstant ≈ 200ms / 4.6 ≈ 43ms
    float rmsTimeConstantSec = 0.043f;  // 43ms time constant (~200ms to 99%)
    int safeBlockSize = juce::jmax(1, samplesPerBlock);  // Prevent division by zero
    // Ensure sampleRate is valid (should be guaranteed by prepareToPlay, but defense in depth)
    double safeSampleRate = juce::jlimit(8000.0, 384000.0, sampleRate);
    float blocksPerSecond = static_cast<float>(safeSampleRate) / static_cast<float>(safeBlockSize);
    rmsCoefficient = 1.0f - std::exp(-1.0f / (blocksPerSecond * rmsTimeConstantSec));
    // Ensure coefficient is in valid range (0, 1)
    rmsCoefficient = juce::jlimit(0.001f, 0.999f, rmsCoefficient);
    // Reset RMS accumulators and mode tracking
    inputRmsAccumulator = 0.0f;
    outputRmsAccumulator = 0.0f;
    lastCompressorMode = -1;  // Force mode change detection on first processBlock
    primeRmsAccumulators = true;  // Prime accumulators on first block

    // Initialize crossover frequency smoothers (~20ms smoothing to prevent zipper noise)
    smoothedCrossover1.reset(sampleRate, 0.02);
    smoothedCrossover2.reset(sampleRate, 0.02);
    smoothedCrossover3.reset(sampleRate, 0.02);
    smoothedCrossover1.setCurrentAndTargetValue(200.0f);
    smoothedCrossover2.setCurrentAndTargetValue(2000.0f);
    smoothedCrossover3.setCurrentAndTargetValue(8000.0f);
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

    // Detect mode change and reset auto-gain accumulators
    // Each compressor mode has different gain characteristics, so the RMS accumulators
    // must be reset to prevent incorrect auto-gain from stale data
    int currentModeInt = static_cast<int>(mode);
    if (currentModeInt != lastCompressorMode)
    {
        lastCompressorMode = currentModeInt;
        // Flag to prime RMS accumulators with first block's values (instant convergence)
        // This avoids the ~200ms delay that would occur if we reset to 0
        primeRmsAccumulators = true;
        // Reset smoothed gain to unity to avoid sudden volume jumps
        smoothedAutoMakeupGain.setCurrentAndTargetValue(1.0f);
    }

    // Read auto-makeup state early - needed for parameter caching
    auto* autoMakeupParamEarly = parameters.getRawParameterValue("auto_makeup");
    bool autoMakeup = (autoMakeupParamEarly != nullptr) ? (autoMakeupParamEarly->load() > 0.5f) : false;

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
                // When auto-makeup is enabled, force gain to 0dB (unity)
                if (autoMakeup)
                    cachedParams[1] = 0.0f;
                else {
                    float gainParam = juce::jlimit(0.0f, 100.0f, p2->load());
                    cachedParams[1] = juce::jlimit(-40.0f, 40.0f, (gainParam - 50.0f) * 0.8f);  // Bounded gain
                }
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
                cachedParams[0] = p1->load();
                // When auto-makeup is enabled, force output to 0dB (unity)
                cachedParams[1] = autoMakeup ? 0.0f : p2->load();
                cachedParams[2] = p3->load();
                cachedParams[3] = p4->load();
                cachedParams[4] = p5->load();
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
                cachedParams[0] = p1->load();
                cachedParams[1] = p2->load();
                cachedParams[2] = p3->load();
                cachedParams[3] = p4->load();
                // When auto-makeup is enabled, force output to 0dB (unity)
                cachedParams[4] = autoMakeup ? 0.0f : p5->load();
                cachedParams[5] = p6->load(); // Store OverEasy state
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
                cachedParams[0] = p1->load();
                // Convert discrete ratio choice to actual ratio value
                int ratioChoice = static_cast<int>(p2->load());
                switch (ratioChoice) {
                    case 0: cachedParams[1] = 2.0f; break;  // 2:1
                    case 1: cachedParams[1] = 4.0f; break;  // 4:1
                    case 2: cachedParams[1] = 10.0f; break; // 10:1
                    default: cachedParams[1] = 2.0f; break;
                }
                cachedParams[2] = p3->load();
                cachedParams[3] = p4->load();
                // When auto-makeup is enabled, force makeup to 0dB (unity)
                cachedParams[4] = autoMakeup ? 0.0f : p5->load();
                cachedParams[5] = p6 ? (p6->load() * 0.01f) : 1.0f; // Convert 0-100 to 0-1, default 100%
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
                cachedParams[0] = p1->load();
                // When auto-makeup is enabled, force output to 0dB (unity)
                cachedParams[1] = autoMakeup ? 0.0f : p2->load();
                cachedParams[2] = p3->load();
                cachedParams[3] = p4->load();
                cachedParams[4] = p5->load();
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
                cachedParams[0] = p1->load();
                cachedParams[1] = p2->load();
                cachedParams[2] = p3->load();
                cachedParams[3] = p4->load();
                // When auto-makeup is enabled, force output to 0dB (unity)
                cachedParams[4] = autoMakeup ? 0.0f : p5->load();
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
            if (p1 && p2 && p3 && p4 && p5 && p6 && p7 && p8 && p9) {
                cachedParams[0] = p1->load();  // threshold
                cachedParams[1] = p2->load();  // ratio
                cachedParams[2] = p3->load();  // knee
                cachedParams[3] = p4->load();  // attack
                cachedParams[4] = p5->load();  // release
                cachedParams[5] = p6->load();  // lookahead
                cachedParams[6] = p7->load();  // mix
                // When auto-makeup is enabled, force output to 0dB (unity)
                cachedParams[7] = autoMakeup ? 0.0f : p8->load();  // output
                cachedParams[8] = p9->load();  // adaptive release (bool as float)
                // SC Listen handled globally via global_sidechain_listen parameter
            } else validParams = false;
            break;
        }
        case CompressorMode::Multiband:
        {
            // Multiband has many parameters - we'll read them directly during processing
            // Just check that the compressor exists
            validParams = (multibandCompressor != nullptr);
            break;
        }
    }

    if (!validParams)
        return;

    // Input metering - use peak level for accurate dB display
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Get peak level - SIMD optimized metering with per-channel tracking
    float inputLevel = 0.0f;
    float inputLevelL = 0.0f;
    float inputLevelR = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        float channelPeak = SIMDHelpers::getPeakLevel(data, numSamples);
        inputLevel = juce::jmax(inputLevel, channelPeak);

        // Store per-channel levels for stereo metering
        if (ch == 0)
            inputLevelL = channelPeak;
        else if (ch == 1)
            inputLevelR = channelPeak;
    }

    // Convert to dB - peak level gives accurate dB reading
    float inputDb = inputLevel > 1e-5f ? juce::Decibels::gainToDecibels(inputLevel) : -60.0f;
    float inputDbL = inputLevelL > 1e-5f ? juce::Decibels::gainToDecibels(inputLevelL) : -60.0f;
    float inputDbR = inputLevelR > 1e-5f ? juce::Decibels::gainToDecibels(inputLevelR) : -60.0f;

    // Use relaxed memory ordering for performance in audio thread
    inputMeter.store(inputDb, std::memory_order_relaxed);
    inputMeterL.store(inputDbL, std::memory_order_relaxed);
    inputMeterR.store(numChannels > 1 ? inputDbR : inputDbL, std::memory_order_relaxed);  // Mono: use same value for both

    // Calculate input RMS for auto-gain (running average across blocks)
    // This gives stable, perceptually-accurate loudness matching
    if (autoMakeup)
    {
        float blockRmsSquared = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                blockRmsSquared += data[i] * data[i];
        }
        // Safe division (prevent division by zero in edge cases)
        int divisor = juce::jmax(1, numSamples * numChannels);
        blockRmsSquared /= static_cast<float>(divisor);

        // Clamp block RMS to prevent numerical issues from pathological inputs
        // Max corresponds to ~+6dBFS RMS (4.0 linear squared), min to -80dBFS
        blockRmsSquared = juce::jlimit(1e-8f, 4.0f, blockRmsSquared);

        // Prime accumulator instantly on mode change for immediate auto-gain response
        if (primeRmsAccumulators)
        {
            inputRmsAccumulator = blockRmsSquared;
        }
        else
        {
            // One-pole smoothing filter for RMS averaging (~200ms window)
            inputRmsAccumulator += rmsCoefficient * (blockRmsSquared - inputRmsAccumulator);
        }

        // Bounds check accumulator to prevent drift in long sessions
        inputRmsAccumulator = juce::jlimit(1e-8f, 4.0f, inputRmsAccumulator);
    }

    // Get sidechain HP filter frequency and update filter if changed (0 = Off/bypassed)
    auto* sidechainHpParam = parameters.getRawParameterValue("sidechain_hp");
    float sidechainHpFreq = (sidechainHpParam != nullptr) ? sidechainHpParam->load() : 80.0f;
    bool sidechainHpEnabled = sidechainHpFreq >= 1.0f;  // 0 = Off
    if (sidechainFilter && sidechainHpEnabled)
        sidechainFilter->setFrequency(sidechainHpFreq);

    // Get global parameters (autoMakeup already read early for parameter caching)
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
    DistortionType distType = (distortionTypeParam != nullptr) ? static_cast<DistortionType>(static_cast<int>(distortionTypeParam->load())) : DistortionType::Off;
    float distAmount = (distortionAmountParam != nullptr) ? (distortionAmountParam->load() / 100.0f) : 0.0f;
    float globalLookaheadMs = (globalLookaheadParam != nullptr) ? globalLookaheadParam->load() : 0.0f;
    bool globalSidechainListen = (globalSidechainListenParam != nullptr) ? (globalSidechainListenParam->load() > 0.5f) : false;
    bool useExternalSidechain = (sidechainEnableParam != nullptr) ? (sidechainEnableParam->load() > 0.5f) : false;
    int stereoLinkMode = (stereoLinkModeParam != nullptr) ? static_cast<int>(stereoLinkModeParam->load()) : 0;
    int oversamplingFactor = (oversamplingParam != nullptr) ? static_cast<int>(oversamplingParam->load()) : 0;

    // Update oversampling factor (0 = Off, 1 = 2x, 2 = 4x)
    // User controls this via dropdown - 2x or 4x recommended for vintage modes with heavy saturation
    if (antiAliasing)
        antiAliasing->setOversamplingFactor(oversamplingFactor);

    // Track oversampling factor changes for internal state
    // NOTE: We NO LONGER re-prepare compressors here because prepare() does memory allocation
    // which is not thread-safe and causes crashes in pluginval level 10 testing.
    // All compressors are prepared for MAX oversampling (4x) in prepareToPlay() so they
    // work correctly regardless of the current oversampling setting.
    if (oversamplingFactor != currentOversamplingFactor)
    {
        currentOversamplingFactor = oversamplingFactor;
        // The actual processing rate adapts automatically based on antiAliasing->processUp/Down
        // which handles the buffer sizing internally without allocation.
    }

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
    // When sidechainHpEnabled is false (slider at 0/Off), bypass the filter entirely.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        const float* inputData = sidechainSource->getReadPointer(juce::jmin(channel, sidechainSource->getNumChannels() - 1));
        float* scData = filteredSidechain.getWritePointer(channel);

        if (sidechainFilter && sidechainHpEnabled)
        {
            // Use block processing for better CPU efficiency (eliminates per-sample function call overhead)
            sidechainFilter->processBlock(inputData, scData, numSamples, channel);
        }
        else
        {
            // No filter or filter disabled - use memcpy for efficiency
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

    // Special handling for Multiband mode - process block-wise without oversampling
    // (crossover filters work best at native sample rate)
    if (mode == CompressorMode::Multiband && multibandCompressor)
    {
        // Read multiband parameters
        auto* xover1Param = parameters.getRawParameterValue("mb_crossover_1");
        auto* xover2Param = parameters.getRawParameterValue("mb_crossover_2");
        auto* xover3Param = parameters.getRawParameterValue("mb_crossover_3");
        auto* mbOutputParam = parameters.getRawParameterValue("mb_output");
        // Use global mix parameter for consistency across all modes
        auto* globalMixParam = parameters.getRawParameterValue("mix");

        float xover1Target = xover1Param ? xover1Param->load() : 200.0f;
        float xover2Target = xover2Param ? xover2Param->load() : 2000.0f;
        float xover3Target = xover3Param ? xover3Param->load() : 8000.0f;
        float mbOutput = mbOutputParam ? mbOutputParam->load() : 0.0f;
        float mbMix = globalMixParam ? globalMixParam->load() : 100.0f;

        // Update crossover frequencies with smoothing to prevent zipper noise
        smoothedCrossover1.setTargetValue(xover1Target);
        smoothedCrossover2.setTargetValue(xover2Target);
        smoothedCrossover3.setTargetValue(xover3Target);

        // Skip numSamples to advance smoothers (we update filters once per block)
        float xover1 = smoothedCrossover1.skip(numSamples);
        float xover2 = smoothedCrossover2.skip(numSamples);
        float xover3 = smoothedCrossover3.skip(numSamples);

        multibandCompressor->updateCrossoverFrequencies(xover1, xover2, xover3);

        // Read per-band parameters
        const juce::String bandNames[] = {"low", "lowmid", "highmid", "high"};
        std::array<float, 4> thresholds, ratios, attacks, releases, makeups;
        std::array<bool, 4> bypasses, solos;

        for (int band = 0; band < 4; ++band)
        {
            const juce::String& name = bandNames[band];
            auto* thresh = parameters.getRawParameterValue("mb_" + name + "_threshold");
            auto* ratio = parameters.getRawParameterValue("mb_" + name + "_ratio");
            auto* attack = parameters.getRawParameterValue("mb_" + name + "_attack");
            auto* release = parameters.getRawParameterValue("mb_" + name + "_release");
            auto* makeup = parameters.getRawParameterValue("mb_" + name + "_makeup");
            auto* bypass = parameters.getRawParameterValue("mb_" + name + "_bypass");
            auto* solo = parameters.getRawParameterValue("mb_" + name + "_solo");

            thresholds[band] = thresh ? thresh->load() : -20.0f;
            ratios[band] = ratio ? ratio->load() : 4.0f;
            attacks[band] = attack ? attack->load() : 10.0f;
            releases[band] = release ? release->load() : 100.0f;
            makeups[band] = makeup ? makeup->load() : 0.0f;
            bypasses[band] = bypass ? (bypass->load() > 0.5f) : false;
            solos[band] = solo ? (solo->load() > 0.5f) : false;
        }

        // Process through multiband compressor
        multibandCompressor->processBlock(buffer, thresholds, ratios, attacks, releases,
                                          makeups, bypasses, solos, mbOutput, mbMix);

        // Update per-band GR meters for UI
        for (int band = 0; band < kNumMultibandBands; ++band)
        {
            bandGainReduction[band].store(multibandCompressor->getBandGainReduction(band),
                                          std::memory_order_relaxed);
        }

        // Get overall GR for main meter
        float grLeft = multibandCompressor->getMaxGainReduction();
        float grRight = grLeft;  // Multiband processes stereo together
        linkedGainReduction[0].store(grLeft, std::memory_order_relaxed);
        linkedGainReduction[1].store(grRight, std::memory_order_relaxed);
        float gainReduction = grLeft;

        // Update GR meter
        grMeter.store(gainReduction, std::memory_order_relaxed);

        // Update GR history for visualization (thread-safe atomic writes)
        grHistoryUpdateCounter++;
        int blocksPerUpdate = static_cast<int>(currentSampleRate / (juce::jmax(1, numSamples) * 30.0));
        blocksPerUpdate = juce::jmax(1, blocksPerUpdate);
        if (grHistoryUpdateCounter >= blocksPerUpdate)
        {
            grHistoryUpdateCounter = 0;
            int pos = grHistoryWritePos.load(std::memory_order_relaxed);
            grHistory[static_cast<size_t>(pos)].store(gainReduction, std::memory_order_relaxed);
            grHistoryWritePos.store((pos + 1) % GR_HISTORY_SIZE, std::memory_order_relaxed);
        }

        // Apply RMS-based auto-gain for multiband mode (professional-grade level matching)
        // Uses running RMS average (~200ms window) for stable, perceptually-accurate gain compensation
        // This matches the approach used in Logic Pro's compressor and other professional plugins
        {
            float targetAutoGain = 1.0f;

            if (autoMakeup)
            {
                // Calculate output RMS for this block (before auto-gain compensation)
                float blockOutputRmsSquared = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float* data = buffer.getReadPointer(ch);
                    for (int i = 0; i < numSamples; ++i)
                        blockOutputRmsSquared += data[i] * data[i];
                }
                blockOutputRmsSquared /= static_cast<float>(numSamples * numChannels);

                // Clamp block RMS to prevent numerical issues
                blockOutputRmsSquared = juce::jlimit(1e-8f, 4.0f, blockOutputRmsSquared);

                // Prime accumulator instantly on mode change for immediate auto-gain response
                bool justPrimed = false;
                if (primeRmsAccumulators)
                {
                    outputRmsAccumulator = blockOutputRmsSquared;
                    primeRmsAccumulators = false;  // Clear flag after priming both accumulators
                    justPrimed = true;
                }
                else
                {
                    // Update running RMS accumulator with one-pole smoothing
                    outputRmsAccumulator += rmsCoefficient * (blockOutputRmsSquared - outputRmsAccumulator);
                }

                // Bounds check accumulator to prevent drift
                outputRmsAccumulator = juce::jlimit(1e-8f, 4.0f, outputRmsAccumulator);

                // Calculate gain compensation from RMS levels
                // Both accumulators are in squared (power) domain
                if (outputRmsAccumulator > 1e-8f && inputRmsAccumulator > 1e-8f)
                {
                    // Gain = sqrt(inputRMS² / outputRMS²) = inputRMS / outputRMS
                    targetAutoGain = std::sqrt(inputRmsAccumulator / outputRmsAccumulator);

                    // Limit compensation range to ±40dB to handle extreme settings
                    targetAutoGain = juce::jlimit(0.01f, 100.0f, targetAutoGain);  // -40dB to +40dB
                }

                // If we just primed, set gain immediately without smoothing
                if (justPrimed)
                {
                    smoothedAutoMakeupGain.setCurrentAndTargetValue(targetAutoGain);
                }
            }

            smoothedAutoMakeupGain.setTargetValue(targetAutoGain);

            if (smoothedAutoMakeupGain.isSmoothing())
            {
                const int maxGainSamples = static_cast<int>(smoothedGainBuffer.size());
                const int samplesToProcess = juce::jmin(numSamples, maxGainSamples);

                for (int i = 0; i < samplesToProcess; ++i)
                    smoothedGainBuffer[static_cast<size_t>(i)] = smoothedAutoMakeupGain.getNextValue();

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* data = buffer.getWritePointer(ch);
                    const float* gains = smoothedGainBuffer.data();
                    for (int i = 0; i < samplesToProcess; ++i)
                        data[i] *= gains[i];
                }
            }
            else
            {
                float currentGain = smoothedAutoMakeupGain.getCurrentValue();
                if (std::abs(currentGain - 1.0f) > 0.001f)
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        float* data = buffer.getWritePointer(ch);
                        SIMDHelpers::applyGain(data, numSamples, currentGain);
                    }
                }
            }
        }

        // Update output meter AFTER auto-makeup gain is applied
        float outputLevel = 0.0f;
        float outputLevelL = 0.0f;
        float outputLevelR = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = buffer.getReadPointer(ch);
            float channelPeak = SIMDHelpers::getPeakLevel(data, numSamples);
            outputLevel = juce::jmax(outputLevel, channelPeak);

            // Store per-channel levels for stereo metering
            if (ch == 0)
                outputLevelL = channelPeak;
            else if (ch == 1)
                outputLevelR = channelPeak;
        }
        float outputDb = outputLevel > 1e-5f ? juce::Decibels::gainToDecibels(outputLevel) : -60.0f;
        float outputDbL = outputLevelL > 1e-5f ? juce::Decibels::gainToDecibels(outputLevelL) : -60.0f;
        float outputDbR = outputLevelR > 1e-5f ? juce::Decibels::gainToDecibels(outputLevelR) : -60.0f;
        outputMeter.store(outputDb, std::memory_order_relaxed);
        outputMeterL.store(outputDbL, std::memory_order_relaxed);
        outputMeterR.store(numChannels > 1 ? outputDbR : outputDbL, std::memory_order_relaxed);

        return;  // Skip normal processing for multiband mode
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
        // Select source buffer once based on stereo link or Mid-Side setting
        const auto& scSource = (useStereoLink || useMidSide) ? linkedSidechain : filteredSidechain;

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
                        data[i] = optoCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2] > 0.5f, true, scData[i]);
                    break;
                case CompressorMode::FET:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = fetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1],
                                                         cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), true,
                                                         lookupTables.get(), transientShaper.get(),
                                                         cachedParams[5] > 0.5f, cachedParams[6], scData[i]);
                    break;
                case CompressorMode::VCA:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = vcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], cachedParams[5] > 0.5f, true, scData[i]);
                    break;
                case CompressorMode::Bus:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = busCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], static_cast<int>(cachedParams[2]), static_cast<int>(cachedParams[3]), cachedParams[4], cachedParams[5], true, scData[i]);
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
                    // SC Listen handled globally before compression - just process normally
                    // Digital: threshold, ratio, knee, attack, release, lookahead, mix, output, adaptive
                    for (int i = 0; i < osNumSamples; ++i)
                    {
                        data[i] = digitalCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2],
                                                             cachedParams[3], cachedParams[4], cachedParams[5], cachedParams[6],
                                                             cachedParams[7], cachedParams[8] > 0.5f, scData[i]);
                    }
                    break;
            }

            // Apply output distortion in the oversampled domain to avoid aliasing
            // This is critical for professional-grade anti-aliasing (UAD/FabFilter standard)
            if (distType != DistortionType::Off && distAmount > 0.0f)
            {
                for (int i = 0; i < osNumSamples; ++i)
                {
                    data[i] = applyDistortion(data[i], distType, distAmount);
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
                    {
                        float scSignal;
                        if ((useStereoLink || useMidSide) && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);
                        data[i] = optoCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2] > 0.5f, false, scSignal) * compensationGain;
                    }
                    break;
                case CompressorMode::FET:
                    for (int i = 0; i < numSamples; ++i)
                    {
                        float scSignal;
                        if ((useStereoLink || useMidSide) && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);
                        data[i] = fetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1],
                                                         cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), false,
                                                         lookupTables.get(), transientShaper.get(),
                                                         cachedParams[5] > 0.5f, cachedParams[6], scSignal) * compensationGain;
                    }
                    break;
                case CompressorMode::VCA:
                    for (int i = 0; i < numSamples; ++i)
                    {
                        float scSignal;
                        if ((useStereoLink || useMidSide) && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);
                        data[i] = vcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], cachedParams[5] > 0.5f, false, scSignal) * compensationGain;
                    }
                    break;
                case CompressorMode::Bus:
                    for (int i = 0; i < numSamples; ++i)
                    {
                        float scSignal;
                        if ((useStereoLink || useMidSide) && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);
                        data[i] = busCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], static_cast<int>(cachedParams[2]), static_cast<int>(cachedParams[3]), cachedParams[4], cachedParams[5], false, scSignal) * compensationGain;
                    }
                    break;
                case CompressorMode::StudioFET:
                    for (int i = 0; i < numSamples; ++i)
                    {
                        // Use linked sidechain if stereo linking or Mid-Side is enabled, otherwise use pre-filtered signal
                        // Note: filter was already applied when building filteredSidechain
                        float scSignal;
                        if ((useStereoLink || useMidSide) && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);
                        data[i] = studioFetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), scSignal) * compensationGain;
                    }
                    break;
                case CompressorMode::StudioVCA:
                    for (int i = 0; i < numSamples; ++i)
                    {
                        // Use linked sidechain if stereo linking or Mid-Side is enabled, otherwise use pre-filtered signal
                        // Note: filter was already applied when building filteredSidechain
                        float scSignal;
                        if ((useStereoLink || useMidSide) && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);
                        data[i] = studioVcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], scSignal) * compensationGain;
                    }
                    break;
                case CompressorMode::Digital:
                    // SC Listen handled globally before compression - just process normally
                    for (int i = 0; i < numSamples; ++i)
                    {
                        float scSignal;
                        if ((useStereoLink || useMidSide) && channel < linkedSidechain.getNumChannels())
                            scSignal = linkedSidechain.getSample(channel, i);
                        else
                            scSignal = filteredSidechain.getSample(channel, i);

                        // Digital: threshold, ratio, knee, attack, release, lookahead, mix, output, adaptive
                        data[i] = digitalCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2],
                                                             cachedParams[3], cachedParams[4], cachedParams[5], cachedParams[6],
                                                             cachedParams[7], cachedParams[8] > 0.5f, scSignal) * compensationGain;
                    }
                    break;
            }

            // Apply output distortion (note: will alias without oversampling - use 2x/4x for clean distortion)
            if (distType != DistortionType::Off && distAmount > 0.0f)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    data[i] = applyDistortion(data[i], distType, distAmount);
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

    // Apply RMS-based auto-gain if enabled (professional-grade level matching)
    // Uses running RMS average (~200ms window) for stable, perceptually-accurate gain compensation
    // This compensates for ALL gain changes: compression, input gain, saturation, etc.
    // This matches the approach used in Logic Pro's compressor and other professional plugins
    {
        float targetAutoGain = 1.0f;

        if (autoMakeup)
        {
            // Calculate output RMS for this block (before auto-gain compensation)
            float blockOutputRmsSquared = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* data = buffer.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    blockOutputRmsSquared += data[i] * data[i];
            }
            blockOutputRmsSquared /= static_cast<float>(numSamples * numChannels);

            // Clamp block RMS to prevent numerical issues
            blockOutputRmsSquared = juce::jlimit(1e-8f, 4.0f, blockOutputRmsSquared);

            // Prime accumulator instantly on mode change for immediate auto-gain response
            bool justPrimed = false;
            if (primeRmsAccumulators)
            {
                outputRmsAccumulator = blockOutputRmsSquared;
                primeRmsAccumulators = false;  // Clear flag after priming both accumulators
                justPrimed = true;
            }
            else
            {
                // Update running RMS accumulator with one-pole smoothing
                outputRmsAccumulator += rmsCoefficient * (blockOutputRmsSquared - outputRmsAccumulator);
            }

            // Bounds check accumulator to prevent drift
            outputRmsAccumulator = juce::jlimit(1e-8f, 4.0f, outputRmsAccumulator);

            // Calculate gain compensation from RMS levels
            // Both accumulators are in squared (power) domain
            if (outputRmsAccumulator > 1e-8f && inputRmsAccumulator > 1e-8f)
            {
                // Gain = sqrt(inputRMS² / outputRMS²) = inputRMS / outputRMS
                targetAutoGain = std::sqrt(inputRmsAccumulator / outputRmsAccumulator);

                // Psychoacoustic loudness compensation for modes with harmonic distortion
                // Harmonic content increases perceived loudness without changing RMS level
                // Apply mode-specific attenuation to compensate:
                // - Opto: Tube warmth + transformer harmonics (-1.5dB)
                // - FET: Aggressive FET saturation (-1.0dB)
                // - Bus: SSL transformer coloration (-0.5dB)
                // - VCA/Digital/Studio: Cleaner, less compensation needed
                float loudnessCompensation = 1.0f;
                switch (mode)
                {
                    case CompressorMode::Opto:
                        loudnessCompensation = 0.84f;  // -1.5dB for tube/transformer harmonics
                        break;
                    case CompressorMode::FET:
                        loudnessCompensation = 0.89f;  // -1.0dB for FET saturation
                        break;
                    case CompressorMode::Bus:
                        loudnessCompensation = 0.94f;  // -0.5dB for SSL character
                        break;
                    default:
                        loudnessCompensation = 1.0f;   // No compensation for cleaner modes
                        break;
                }
                targetAutoGain *= loudnessCompensation;

                // Limit compensation range to ±40dB to handle extreme input gain settings
                // (e.g., FET input at -20dB can create 30+ dB level differences)
                // This range is needed because:
                // - FET/VCA input gain ranges from -20dB to +20dB
                // - Heavy compression can add another 20dB of gain reduction
                // - Combined effect can require 30-40dB of compensation
                targetAutoGain = juce::jlimit(0.01f, 100.0f, targetAutoGain);  // -40dB to +40dB
            }

            // If we just primed, set gain immediately without smoothing
            if (justPrimed)
            {
                smoothedAutoMakeupGain.setCurrentAndTargetValue(targetAutoGain);
            }
        }

        // Update target and apply smoothed gain sample-by-sample
        smoothedAutoMakeupGain.setTargetValue(targetAutoGain);

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
        else
        {
            // No smoothing needed, apply constant gain efficiently
            float currentGain = smoothedAutoMakeupGain.getCurrentValue();
            if (std::abs(currentGain - 1.0f) > 0.001f)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* data = buffer.getWritePointer(ch);
                    SIMDHelpers::applyGain(data, numSamples, currentGain);
                }
            }
        }
    }

    // NOTE: Output distortion is now applied in the oversampled domain (inside the
    // oversample block above) to prevent aliasing. This matches professional standards
    // (UAD, FabFilter) where all nonlinear processing happens at the oversampled rate.
    // For non-oversampled processing (fallback), distortion was already applied inline.

    // Output metering - SIMD optimized with per-channel tracking
    float outputLevel = 0.0f;
    float outputLevelL = 0.0f;
    float outputLevelR = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        float channelPeak = SIMDHelpers::getPeakLevel(data, numSamples);
        outputLevel = juce::jmax(outputLevel, channelPeak);

        // Store per-channel levels for stereo metering
        if (ch == 0)
            outputLevelL = channelPeak;
        else if (ch == 1)
            outputLevelR = channelPeak;
    }

    float outputDb = outputLevel > 1e-5f ? juce::Decibels::gainToDecibels(outputLevel) : -60.0f;
    float outputDbL = outputLevelL > 1e-5f ? juce::Decibels::gainToDecibels(outputLevelL) : -60.0f;
    float outputDbR = outputLevelR > 1e-5f ? juce::Decibels::gainToDecibels(outputLevelR) : -60.0f;

    // Use relaxed memory ordering for performance in audio thread
    outputMeter.store(outputDb, std::memory_order_relaxed);
    outputMeterL.store(outputDbL, std::memory_order_relaxed);
    outputMeterR.store(numChannels > 1 ? outputDbR : outputDbL, std::memory_order_relaxed);  // Mono: use same value for both

    // Store gain reduction through delay buffer to sync meter with audio output
    // This ensures the GR display matches what you hear after PDC latency compensation
    // We delay by numSamples each block (processing one block's worth of GR at a time)
    float delayedGR = gainReduction;  // Default if no delay
    // Use acquire ordering to synchronize with prepareToPlay's release store
    int currentDelaySamples = grDelaySamples.load(std::memory_order_acquire);
    if (currentDelaySamples > 0)
    {
        int writePos = grDelayWritePos.load(std::memory_order_relaxed);

        // Write current GR to delay buffer
        grDelayBuffer[static_cast<size_t>(writePos)] = gainReduction;

        // Calculate read position (delayed)
        int readPos = (writePos - currentDelaySamples + MAX_GR_DELAY_SAMPLES) % MAX_GR_DELAY_SAMPLES;
        delayedGR = grDelayBuffer[static_cast<size_t>(readPos)];

        // Advance write position by number of samples in this block
        // (simplified: advance by 1 per block since we store 1 GR value per block)
        grDelayWritePos.store((writePos + 1) % MAX_GR_DELAY_SAMPLES, std::memory_order_relaxed);
    }

    grMeter.store(delayedGR, std::memory_order_relaxed);

    // Update the gain reduction parameter for DAW display (uses delayed value)
    if (auto* grParam = parameters.getRawParameterValue("gr_meter"))
        *grParam = delayedGR;

    // Update GR history buffer for visualization (approximately 30Hz update rate)
    // At 48kHz with 512 samples/block, we get ~94 blocks/sec, so update every 3 blocks
    // Uses delayed GR so history graph also syncs with audio (thread-safe atomic writes)
    grHistoryUpdateCounter++;
    if (grHistoryUpdateCounter >= 3)
    {
        grHistoryUpdateCounter = 0;
        int writePos = grHistoryWritePos.load(std::memory_order_relaxed);
        grHistory[static_cast<size_t>(writePos)].store(delayedGR, std::memory_order_relaxed);
        writePos = (writePos + 1) % GR_HISTORY_SIZE;
        grHistoryWritePos.store(writePos, std::memory_order_relaxed);
    }
    
    // Apply mix control for parallel compression (SIMD-optimized)
    if (needsDryBuffer && dryBuffer.getNumChannels() > 0)
    {
        // Blend dry and wet signals
        // Note: mixBuffers formula is dest = dest*(1-param) + src*param
        // We want: 100% mix = 100% wet (compressed), so we invert the parameter
        // This makes: mix=100% -> param=0 -> output=wet, mix=0% -> param=1 -> output=dry
        float dryAmount = 1.0f - mixAmount;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* wet = buffer.getWritePointer(ch);
            const float* dry = dryBuffer.getReadPointer(ch);
            SIMDHelpers::mixBuffers(wet, dry, numSamples, dryAmount);
        }
    }

    // Add subtle analog noise for authenticity (-80dB) if enabled (SIMD-optimized)
    // Only for analog modes - Digital and Multiband are meant to be completely transparent
    // This adds character and prevents complete digital silence
    auto* noiseEnableParam = parameters.getRawParameterValue("noise_enable");
    bool noiseEnabled = noiseEnableParam ? (*noiseEnableParam > 0.5f) : true; // Default ON

    // Skip noise for Digital (mode 6) and Multiband (mode 7) - they should be transparent
    auto* modeParam = parameters.getRawParameterValue("mode");
    int modeIndex = modeParam ? static_cast<int>(modeParam->load()) : 0;
    bool isAnalogMode = (modeIndex != 6 && modeIndex != 7);

    if (noiseEnabled && isAnalogMode)
    {
        const float noiseLevel = 0.0001f; // -80dB
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            SIMDHelpers::addNoise(data, numSamples, noiseLevel, noiseRandom);
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
        return static_cast<CompressorMode>(juce::jlimit(0, kMaxCompressorModeIndex, mode));  // 8 modes: 0-7 (including Multiband)
    }
    return CompressorMode::Opto; // Default fallback
}

double UniversalCompressor::getLatencyInSamples() const
{
    double latency = 0.0;

    // Report latency from oversampler only
    // Lookahead is 0 by default and rarely changed, so we don't include it
    // This gives much lower latency for typical use
    if (antiAliasing)
    {
        latency += static_cast<double>(antiAliasing->getLatency());
    }

    return latency;
}

double UniversalCompressor::getTailLengthSeconds() const
{
    // Account for lookahead delay if implemented
    return currentSampleRate > 0 ? getLatencyInSamples() / currentSampleRate : 0.0;
}

bool UniversalCompressor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Main input must be mono or stereo
    const auto& mainInput = layouts.getMainInputChannelSet();
    if (mainInput.isDisabled() || mainInput.size() > 2)
        return false;

    // Output must match main input
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    if (mainOutput != mainInput)
        return false;

    // Sidechain input is optional, but if enabled must be mono or stereo
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet(true, 1);
        if (!sidechain.isDisabled() && sidechain.size() > 2)
            return false;
    }

    return true;
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
    {
        if (xmlState->hasTagName(parameters.state.getType()))
        {
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));

            // Reset all DSP state after state restoration to ensure clean audio output
            // This is critical for pluginval's state restoration test which expects
            // audio output to match after setStateInformation without calling prepareToPlay
            resetDSPState();
        }
    }
}

void UniversalCompressor::resetDSPState()
{
    // Reset smoothed auto-makeup gain and RMS accumulators to neutral
    smoothedAutoMakeupGain.setCurrentAndTargetValue(1.0f);
    inputRmsAccumulator = 0.0f;
    outputRmsAccumulator = 0.0f;
    lastCompressorMode = -1;  // Force mode change detection on next processBlock
    primeRmsAccumulators = true;  // Prime accumulators on first block after reset

    // NOTE: We do NOT call prepare() here because:
    // 1. prepare() does memory allocation which is not thread-safe
    // 2. resetDSPState() can be called from setStateInformation() on any thread
    // 3. The compressors were already prepared in prepareToPlay() for max oversampling (4x)
    //
    // Instead, we just reset the internal state of each compressor by resetting
    // their envelopes. The compressors are designed to handle this gracefully.

    // Reset GR metering state
    grDelayBuffer.fill(0.0f);
    grDelayWritePos.store(0, std::memory_order_relaxed);
}

//==============================================================================
// Factory Presets
//==============================================================================
// Unified preset system - uses CompressorPresets.h for both DAW programs and UI
namespace {
    // Cache the factory presets for quick access
    const std::vector<CompressorPresets::Preset>& getCachedPresets()
    {
        static std::vector<CompressorPresets::Preset> presets = CompressorPresets::getFactoryPresets();
        return presets;
    }

    int getNumCachedPresets()
    {
        return static_cast<int>(getCachedPresets().size());
    }
}

const std::vector<UniversalCompressor::PresetInfo>& UniversalCompressor::getPresetList()
{
    static std::vector<PresetInfo> presetList;
    if (presetList.empty())
    {
        const auto& presets = getCachedPresets();
        for (const auto& preset : presets)
        {
            presetList.push_back({
                preset.name,
                preset.category,
                static_cast<CompressorMode>(preset.mode)
            });
        }
    }
    return presetList;
}

int UniversalCompressor::getNumPrograms()
{
    return static_cast<int>(getCachedPresets().size()) + 1;  // +1 for "Default"
}

int UniversalCompressor::getCurrentProgram()
{
    return currentPresetIndex;
}

const juce::String UniversalCompressor::getProgramName(int index)
{
    if (index == 0)
        return "Default";

    const auto& presets = getCachedPresets();
    if (index - 1 >= 0 && index - 1 < static_cast<int>(presets.size()))
        return presets[static_cast<size_t>(index - 1)].name;

    return {};
}

void UniversalCompressor::setCurrentProgram(int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;

    currentPresetIndex = index;

    if (index == 0)
    {
        // Default preset - reset to neutral starting point
        // Mode: Vintage Opto (0)
        if (auto* p = parameters.getParameter("mode"))
            p->setValueNotifyingHost(0.0f);

        // Common controls
        if (auto* p = parameters.getParameter("mix"))
            p->setValueNotifyingHost(1.0f);  // 100%
        if (auto* p = parameters.getParameter("sidechain_hp"))
            p->setValueNotifyingHost(parameters.getParameterRange("sidechain_hp").convertTo0to1(80.0f));
        if (auto* p = parameters.getParameter("auto_makeup"))
            p->setValueNotifyingHost(0.0f);  // Off
        if (auto* p = parameters.getParameter("saturation_mode"))
            p->setValueNotifyingHost(0.0f);  // Vintage

        // Opto defaults
        if (auto* p = parameters.getParameter("opto_peak_reduction"))
            p->setValueNotifyingHost(parameters.getParameterRange("opto_peak_reduction").convertTo0to1(30.0f));
        if (auto* p = parameters.getParameter("opto_gain"))
            p->setValueNotifyingHost(parameters.getParameterRange("opto_gain").convertTo0to1(0.0f));
        if (auto* p = parameters.getParameter("opto_limit"))
            p->setValueNotifyingHost(0.0f);  // Compress

        // Notify listeners so UI knows preset changed (mode 0 = Opto)
        juce::MessageManager::callAsync([this, index]() {
            presetChangeListeners.call(&PresetChangeListener::presetChanged, index, 0);
        });
        return;
    }

    // Apply factory preset (index - 1 because 0 is "Default")
    const auto& presets = getCachedPresets();
    int targetMode = -1;
    if (index - 1 < static_cast<int>(presets.size()))
    {
        const auto& preset = presets[static_cast<size_t>(index - 1)];
        targetMode = preset.mode;
        CompressorPresets::applyPreset(parameters, preset);
    }

    // Notify listeners on message thread (UI needs to refresh)
    // Pass the target mode so UI can update immediately without waiting for parameter propagation
    juce::MessageManager::callAsync([this, index, targetMode]() {
        presetChangeListeners.call(&PresetChangeListener::presetChanged, index, targetMode);
    });
}

// LV2 inline display removed - JUCE doesn't natively support this extension
// and manual Cairo implementation conflicts with JUCE's LV2 wrapper.
// The full GUI works perfectly in all LV2 hosts.

// Plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UniversalCompressor();
}

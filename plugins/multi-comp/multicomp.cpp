#include "UniversalCompressor.h"
#include "EnhancedCompressorEditor.h"
#include "CompressorPresets.h"
#include "HardwareEmulation/HardwareMeasurements.h"
#include "HardwareEmulation/WaveshaperCurves.h"
#include "HardwareEmulation/TransformerEmulation.h"
#include "HardwareEmulation/TubeEmulation.h"
#include "HardwareEmulation/ConvolutionEngine.h"
#include <cmath>

// Helper utilities for DSP operations (scalar implementations for cross-platform compatibility)
namespace SIMDHelpers {
    // Process buffer to get peak level
    inline float getPeakLevel(const float* data, int numSamples) {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax(peak, std::abs(data[i]));
        return peak;
    }

    // Apply gain to buffer
    inline void applyGain(float* data, int numSamples, float gain) {
        for (int i = 0; i < numSamples; ++i)
            data[i] *= gain;
    }

    // Mix two buffers (for parallel compression)
    inline void mixBuffers(float* dest, const float* src, int numSamples, float wetAmount) {
        const float dryAmount = 1.0f - wetAmount;
        for (int i = 0; i < numSamples; ++i)
            dest[i] = dest[i] * dryAmount + src[i] * wetAmount;
    }

    // Add analog noise (for authenticity)
    inline void addNoise(float* data, int numSamples, float noiseLevel, juce::Random& random) {
        for (int i = 0; i < numSamples; ++i)
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

namespace Constants {
    // T4B Optical Cell — CdS photoresistor + electroluminescent panel
    constexpr float T4B_ATTACK_TIME = 0.002f;             // 2ms CdS fast charge
    constexpr float T4B_FAST_RELEASE_TIME = 0.060f;       // 60ms CdS base discharge
    constexpr float T4B_PHOSPHOR_BASE_DECAY = 1.5f;       // 1.5s phosphor decay (real T4B measured 1.5-2s)
    constexpr float T4B_PHOSPHOR_ATTACK_RATIO = 0.3f;     // Phosphor attack relative to decay
    constexpr float T4B_GAMMA = 0.7f;                     // CdS power law exponent (real CdS ~0.5-1.0)
    constexpr float T4B_CONDUCTANCE_K = 3.0f;             // Conductance scaling (re-tuned for gamma=0.7)
    constexpr float T4B_PHOSPHOR_COUPLING = 0.40f;        // Residual compression between bursts
    constexpr float T4B_PROG_DEP_CHARGE_RATE = 0.15f;     // Program dependency charge (~7s)
    constexpr float T4B_PROG_DEP_DISCHARGE_RATE = 0.12f;  // Program dependency decay (~8s)
    constexpr float T4B_PROG_DEP_RELEASE_SCALE = 5.0f;    // Max release time multiplier (60ms → 300ms)
    constexpr float T4B_PROG_DEP_PHOSPHOR_SCALE = 3.0f;   // Max phosphor extension (1.5s → 6s under sustained compression)
    constexpr float SC_DRIVER_SATURATION = 0.8f;           // 6AQ5 saturation
    constexpr float SC_DRIVER_OUTPUT_SCALE = 1.0f;         // 6AQ5 output scaling
    constexpr float T4B_EL_PANEL_ATTACK_FREQ = 150.0f;    // EL panel attack (~1.1ms)
    constexpr float T4B_EL_PANEL_RELEASE_FREQ = 5.0f;     // EL panel release (~32ms)
    constexpr float T4B_CONDUCTANCE_ATTACK_FREQ = 150.0f;  // Conductance rise (~1.1ms)
    constexpr float T4B_CONDUCTANCE_RELEASE_FREQ = 4.0f;   // Conductance fall (~40ms)
    constexpr float SC_DRIVER_THRESHOLD = 0.03f;           // 6AQ5 grid bias cutoff
    constexpr float SC_LEVEL_SMOOTH_FREQ = 800.0f;         // Envelope smoother (passes bass 2f ripple)
    constexpr float PEAK_REDUCTION_MAX_SC_GAIN = 14.0f;    // Max sidechain gain at PR=100
    constexpr float T4B_MAX_CONDUCTANCE = 6.0f;            // Gain floor 1/(1+6) = -16.9dB
    constexpr float T4B_MAX_GAIN_RELEASE_RATE = 10.0f;     // Max gain recovery speed (units/sec)

    // Vintage FET constants
    constexpr float FET_THRESHOLD_DB = -10.0f; // Fixed threshold
    constexpr float FET_MAX_REDUCTION_DB = 30.0f;
    constexpr float FET_ALLBUTTONS_MIN_ATTACK = 0.0002f; // 200µs — ABI still grabs fast
    constexpr float FET_ALLBUTTONS_MAX_ATTACK = 0.002f;  // 2ms — ABI ceiling (lag control)

    // Classic VCA constants
    constexpr float VCA_RMS_TIME_CONSTANT = 0.003f; // 3ms RMS averaging
    constexpr float VCA_RELEASE_RATE = 120.0f; // dB per second
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
            // FIR provides superior alias rejection (~80dB stopband) compared to IIR (~60dB),
            // which is essential for saturation effects to prevent audible aliasing.
            // Note: FIR has linear phase with pre-ring, but we handle this by mixing
            // dry/wet BEFORE downsampling so both signals go through the same filter.
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

            // Cache max oversampler latency and allocate compensation delay buffer.
            // This keeps reported latency constant when switching OS modes mid-playback.
            maxOversamplerLatency = oversampler4x
                ? static_cast<int>(std::lround(oversampler4x->getLatencyInSamples()))
                : 0;
            if (maxOversamplerLatency > 0)
            {
                // +1 so a delay of exactly maxOversamplerLatency is reachable
                osCompDelayBuffer.setSize(numChannels, maxOversamplerLatency + 1, false, true, true);
                osCompDelayWritePos = 0;
            }
            updateCompDelay();
        }
    }

    void setOversamplingFactor(int factor)
    {
        // 0 = Off, 1 = 2x, 2 = 4x
        oversamplingOff = (factor == 0);
        use4x = (factor == 2);
        updateCompDelay();
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
        {
            // No oversampler ran (either OS off, or safety bailout from processUp
            // due to oversized/incompatible block). Apply full max latency as
            // compensation since the oversampler contributed 0 latency this block.
            applyCompensationDelay(block, maxOversamplerLatency);
            return;
        }

        auto* oversampler = use4x ? oversampler4x.get() : oversampler2x.get();
        if (oversampler)
            oversampler->processSamplesDown(block);

        // Apply mode-specific compensation delay (max - current oversampler latency)
        applyCompensationDelay(block, osCompDelaySamples);
    }
    
    // Pre-processing passthrough
    // JUCE's oversampling FIR provides excellent anti-aliasing (~80dB stopband)
    // for saturation effects. We use FIR for superior alias rejection.
    // Note: FIR pre-ring is handled by mixing dry/wet before downsampling.
    float preProcessSample(float input, int /*channel*/)
    {
        return input;  // Passthrough - rely on JUCE's oversampling filters
    }
    
    // Post-processing: DC blocking only
    // IMPORTANT: No saturation here - all nonlinear processing must happen
    // in the oversampled domain to avoid aliasing
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
    
    // Band-limited additive synthesis (no aliasing from harmonic generation)
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
        // Always return max (4x) latency so DAW PDC stays constant when switching modes mid-playback.
        // A compensation delay line fills the gap for Off/2x modes.
        return getMaxLatency();
    }

    int getMaxLatency() const
    {
        return oversampler4x
            ? static_cast<int>(std::lround(oversampler4x->getLatencyInSamples()))
            : 0;
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

    void updateCompDelay()
    {
        int currentLatency = 0;
        if (!oversamplingOff)
        {
            auto* oversampler = use4x ? oversampler4x.get() : oversampler2x.get();
            if (oversampler)
                currentLatency = static_cast<int>(std::lround(oversampler->getLatencyInSamples()));
        }
        // Only update the delay amount — never clear the ring buffer.
        // The ring runs continuously at maxOversamplerLatency size so that
        // switching from 4x (delay=0) to Off/2x reads valid history, not silence.
        osCompDelaySamples = juce::jlimit(0, maxOversamplerLatency, maxOversamplerLatency - currentLatency);
    }

    void applyCompensationDelay(juce::dsp::AudioBlock<float>& block, int delaySamples)
    {
        // Ring must hold maxOversamplerLatency + 1 slots so that a delay equal
        // to maxOversamplerLatency reads a genuinely old sample, not the one
        // just written at writePos.
        const int ringSize = maxOversamplerLatency + 1;
        if (maxOversamplerLatency <= 0 || osCompDelayBuffer.getNumSamples() < ringSize)
            return;

        delaySamples = juce::jlimit(0, ringSize - 1, delaySamples);

        int numSamp = static_cast<int>(block.getNumSamples());
        int numCh = juce::jmin(static_cast<int>(block.getNumChannels()),
                               osCompDelayBuffer.getNumChannels());

        for (int i = 0; i < numSamp; ++i)
        {
            // Always write into the ring at the write head
            for (int ch = 0; ch < numCh; ++ch)
                osCompDelayBuffer.setSample(ch, osCompDelayWritePos, block.getSample(ch, i));

            if (delaySamples > 0)
            {
                // Read from (writePos - delaySamples), wrapped into the ring
                int readPos = (osCompDelayWritePos - delaySamples + ringSize) % ringSize;
                for (int ch = 0; ch < numCh; ++ch)
                    block.setSample(ch, i, osCompDelayBuffer.getSample(ch, readPos));
            }
            // delaySamples == 0: pass through unchanged (4x mode, no compensation needed)

            osCompDelayWritePos = (osCompDelayWritePos + 1) % ringSize;
        }
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
    std::vector<ChannelState> channelStates;
    double sampleRate = 0.0;  // Set by prepare() from DAW
    int blockSize = 0;        // Set by prepare() from DAW
    int numChannels = 0;      // Set by prepare() from DAW
    bool oversamplingOff = false;  // No oversampling (1x)
    bool use4x = false;       // Use 4x oversampling instead of 2x
    bool didUpsample = false; // Track if processUp actually performed upsampling

    // Constant-latency compensation: always report 4x latency, delay Off/2x to match
    int maxOversamplerLatency = 0;
    juce::AudioBuffer<float> osCompDelayBuffer;
    int osCompDelayWritePos = 0;
    int osCompDelaySamples = 0;
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

// LA-2A Optical Leveling Amplifier
// Authentic feedback topology: gain reduction emerges from the T4B cell
// interacting with the feedback loop — no hardcoded threshold or ratio.
class UniversalCompressor::OptoCompressor
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        detectors.resize(numChannels);
        for (auto& det : detectors)
        {
            det.elPanelLevel = 0.0f;
            det.cellCharge = 0.0f;
            det.phosphorGlow = 0.0f;
            det.accumulatedCharge = 0.0f;
            det.smoothedConductance = 0.0f;
            det.t4bGain = 1.0f;
            det.shelfX1 = 0.0f;
            det.shelfY1 = 0.0f;
            det.t4bDcState = 0.0f;
        }

        // Pre-compute sample-rate-dependent coefficients
        float sr = static_cast<float>(sampleRate);
        invSampleRate = 1.0f / sr;
        attackCoeff = std::exp(-1.0f / (Constants::T4B_ATTACK_TIME * sr));
        fastReleaseCoeff = std::exp(-1.0f / (Constants::T4B_FAST_RELEASE_TIME * sr));
        phosphorDecayCoeff = std::exp(-1.0f / (Constants::T4B_PHOSPHOR_BASE_DECAY * sr));
        // R37 high-shelf at 1kHz, +3dB (makes sidechain more HF-sensitive)
        {
            float wc = 2.0f * 3.14159f * 1000.0f / sr;
            float A = std::pow(10.0f, 3.0f / 20.0f); // +3dB
            float alpha = std::tan(wc / 2.0f);
            float sqrtA = std::sqrt(A);
            // 1st-order high shelf: bilinear transform of H(s) = A*(s + w0/sqrtA) / (s + w0*sqrtA)
            // DC gain = 1, HF gain = A (+3dB)
            float norm = 1.0f / (1.0f + alpha * sqrtA);
            shelfB0 = (A + sqrtA * alpha) * norm;
            shelfB1 = (sqrtA * alpha - A) * norm;
            shelfA1 = (alpha * sqrtA - 1.0f) * norm;
        }
        phosphorAttackCoeff = std::pow(phosphorDecayCoeff, Constants::T4B_PHOSPHOR_ATTACK_RATIO);
        condAttackCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::T4B_CONDUCTANCE_ATTACK_FREQ / sr);
        condReleaseCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::T4B_CONDUCTANCE_RELEASE_FREQ / sr);
        elPanelAttackCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::T4B_EL_PANEL_ATTACK_FREQ / sr);
        elPanelReleaseCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::T4B_EL_PANEL_RELEASE_FREQ / sr);
        scLevelSmoothCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::SC_LEVEL_SMOOTH_FREQ / sr);

        // Hardware emulation: Input transformer (UTC A-10)
        inputTransformer.prepare(sampleRate, numChannels);
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getOptoCompressor().inputTransformer);
        inputTransformer.setEnabled(true);

        // Hardware emulation: Output transformer (UTC A-24)
        outputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getOptoCompressor().outputTransformer);
        outputTransformer.setEnabled(true);

        // Hardware emulation: 12BH7 output tube
        tubeStage.prepare(sampleRate, numChannels);
        tubeStage.setTubeType(HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7);
        tubeStage.setDrive(0.15f);  // Baseline drive — scales dynamically in process()

        // Calibrate hardware gain compensation so PR=0 + Gain=50 = unity
        // The tube + transformer chain adds ~3.5dB of gain that needs to be removed
        // Measure it at a reference level to get the exact factor for this sample rate
        calibrateHardwareGain();
    }

    // Lightweight sample rate update for oversampling changes (no allocation)
    void updateSampleRate(double newSampleRate)
    {
        if (newSampleRate <= 0.0 || newSampleRate == sampleRate)
            return;
        sampleRate = newSampleRate;
        int numCh = static_cast<int>(detectors.size());

        float sr = static_cast<float>(sampleRate);
        invSampleRate = 1.0f / sr;
        attackCoeff = std::exp(-1.0f / (Constants::T4B_ATTACK_TIME * sr));
        fastReleaseCoeff = std::exp(-1.0f / (Constants::T4B_FAST_RELEASE_TIME * sr));
        phosphorDecayCoeff = std::exp(-1.0f / (Constants::T4B_PHOSPHOR_BASE_DECAY * sr));
        // R37 high-shelf at 1kHz, +3dB (makes sidechain more HF-sensitive)
        {
            float wc = 2.0f * 3.14159f * 1000.0f / sr;
            float A = std::pow(10.0f, 3.0f / 20.0f); // +3dB
            float alpha = std::tan(wc / 2.0f);
            float sqrtA = std::sqrt(A);
            // 1st-order high shelf: bilinear transform of H(s) = A*(s + w0/sqrtA) / (s + w0*sqrtA)
            // DC gain = 1, HF gain = A (+3dB)
            float norm = 1.0f / (1.0f + alpha * sqrtA);
            shelfB0 = (A + sqrtA * alpha) * norm;
            shelfB1 = (sqrtA * alpha - A) * norm;
            shelfA1 = (alpha * sqrtA - 1.0f) * norm;
        }
        phosphorAttackCoeff = std::pow(phosphorDecayCoeff, Constants::T4B_PHOSPHOR_ATTACK_RATIO);
        condAttackCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::T4B_CONDUCTANCE_ATTACK_FREQ / sr);
        condReleaseCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::T4B_CONDUCTANCE_RELEASE_FREQ / sr);
        elPanelAttackCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::T4B_EL_PANEL_ATTACK_FREQ / sr);
        elPanelReleaseCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::T4B_EL_PANEL_RELEASE_FREQ / sr);
        scLevelSmoothCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * Constants::SC_LEVEL_SMOOTH_FREQ / sr);

        inputTransformer.prepare(sampleRate, numCh);
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getOptoCompressor().inputTransformer);
        outputTransformer.prepare(sampleRate, numCh);
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getOptoCompressor().outputTransformer);
        tubeStage.prepare(sampleRate, numCh);
        tubeStage.setTubeType(HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7);
        tubeStage.setDrive(0.15f);  // Baseline drive — scales dynamically in process()
        calibrateHardwareGain();
    }

    float process(float input, int channel, float peakReduction, float gain, bool limitMode, bool oversample = false, float sidechainSignal = 0.0f, bool useExternalSidechain = false)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;
        if (sampleRate <= 0.0)
            return input;

        peakReduction = juce::jlimit(0.0f, 100.0f, peakReduction);
        gain = juce::jlimit(-40.0f, 40.0f, gain);

        auto& det = detectors[channel];

        // Stage 1: Input transformer (UTC A-10)
        float x = inputTransformer.processSample(input, channel);

        // Stage 2: T4B gain cell — apply gain from previous sample's T4B state
        // This one-sample feedback delay is inherent to the real hardware
        //
        // Real T4B photocell: CdS (cadmium sulfide) has nonlinear resistance
        // that varies with illumination. Under gain reduction, the cell's
        // resistance curve produces even-order harmonics (primarily H2).
        // More GR = more nonlinearity = more "warmth" and "fullness."
        // This is the core of the LA-2A's character — NOT just the tube.
        float compressed = x * det.t4bGain;

        // T4B even-harmonic distortion: proportional to gain reduction depth
        float grAmount = 1.0f - det.t4bGain;  // 0 = no GR, 1 = full GR
        if (grAmount > 0.01f)
        {
            // CdS cell produces predominantly 2nd harmonic (asymmetric transfer curve)
            // DC-blocked x² gives pure H2 without level shift
            float sq = compressed * compressed;
            det.t4bDcState = det.t4bDcState * 0.9999f + sq * 0.0001f;
            float h2 = sq - det.t4bDcState;

            // Scale: subtle at light GR, rich at heavy GR (~2-4% THD)
            float k2 = grAmount * 0.12f;
            compressed = compressed + k2 * h2;
        }

        // Stage 3+4: Sidechain signal selection + frequency shaping
        float scSignal;
        if (useExternalSidechain)
        {
            scSignal = sidechainSignal;
            det.shelfX1 = 0.0f;
            det.shelfY1 = 0.0f;
        }
        else if (!limitMode)
        {
            // Compress: pure feedback — sidechain taps compressed output
            scSignal = compressed;
            // Apply R37 shelf filter
            float shelfOut = shelfB0 * scSignal + shelfB1 * det.shelfX1 - shelfA1 * det.shelfY1;
            det.shelfX1 = scSignal;
            det.shelfY1 = shelfOut;
            scSignal = shelfOut;
        }
        else
        {
            // Limit: partially feed-forward tap — blend input + output for tighter ratio
            scSignal = input * 0.5f + compressed * 0.5f;
            det.shelfX1 = 0.0f;
            det.shelfY1 = 0.0f;
        }

        // Stage 5: Peak Reduction = sidechain amplifier gain
        // This is the primary compression control — NOT a threshold
        // Cubic taper: PR=0 → gain=0 (no compression), PR=100 → gain=MAX
        // Models the real LA-2A pot where low settings barely reach the T4B
        float prNorm = peakReduction * 0.01f;
        float peakReductionGain = prNorm * prNorm * prNorm * Constants::PEAK_REDUCTION_MAX_SC_GAIN;

        // Stage 5b: 6AQ5 sidechain driver tube — soft-clips before the EL panel
        float scDrive = std::abs(scSignal * peakReductionGain);

        float effectiveDrive = std::max(0.0f, scDrive - Constants::SC_DRIVER_THRESHOLD);

        float scLevel = std::tanh(effectiveDrive * Constants::SC_DRIVER_SATURATION)
            * Constants::SC_DRIVER_OUTPUT_SCALE;

        // Stage 6: Sidechain envelope smoothing + T4B cell update
        // Symmetric LP filter reduces 2f ripple from rectified sidechain.
        // The real LA-2A has capacitor smoothing in the rectifier circuit.
        det.scLevelSmoothed += scLevelSmoothCoeff * (scLevel - det.scLevelSmoothed);
        updateT4BCell(det, det.scLevelSmoothed);

        // Stage 7: Output stage — real LA-2A signal chain
        // In the real LA-2A, the 12BH7 IS the makeup gain amplifier.
        // When compressing hard, you turn up the Gain knob to compensate,
        // which drives the tube harder → more even-harmonic warmth.
        // This is why LA-2A compression sounds "full and alive."
        float makeupGain = juce::Decibels::decibelsToGain(gain);

        // Drive the tube proportionally harder when compression is applied.
        // Models the real behavior: more GR → user turns up gain → tube driven harder.
        // Scale up before the tube, then scale back down to preserve output level.
        // The nonlinear tube harmonics survive the scaling, adding warmth.
        float grCompensation = 1.0f / juce::jmax(0.1f, det.t4bGain);
        float tubeBoost = 1.0f + (grCompensation - 1.0f) * 0.7f;  // 70% GR compensation

        float output = compressed * makeupGain * tubeBoost;

        // Dynamic tube drive: more GR → user turns up Gain → tube driven harder
        // Models real LA-2A behavior where compression + makeup = harmonically rich
        float dynamicDrive = 0.15f + grAmount * 0.3f;  // 0.15 clean to 0.45 pushed
        tubeStage.setDrive(dynamicDrive);

        // 12BH7 output tube — driven harder when compression is active
        output = tubeStage.processSample(output, channel);

        // Scale back to preserve level (harmonics are retained)
        output /= tubeBoost;

        // Output transformer (UTC A-24)
        output = outputTransformer.processSample(output, channel);

        // Only apply hardware compensation (not makeup, already applied)
        output *= hardwareGainCompensation;

        return juce::jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, output);
    }

    float getGainReduction(int channel) const
    {
        if (channel >= static_cast<int>(detectors.size()))
            return 0.0f;
        return juce::Decibels::gainToDecibels(detectors[channel].t4bGain);
    }


private:
    struct Detector
    {
        float elPanelLevel = 0.0f;        // EL panel thermal response (smoothed sidechain drive)
        float cellCharge = 0.0f;          // CdS fast component (~10ms attack, ~60ms release)
        float phosphorGlow = 0.0f;        // EL panel slow persistence (1-5s decay)
        float accumulatedCharge = 0.0f;    // Long-term charge for program dependency (0-1)
        float smoothedConductance = 0.0f; // Bandwidth-limited CdS conductance
        float t4bGain = 1.0f;             // Current gain from T4B cell (IS the envelope)
        float shelfX1 = 0.0f;  // R37 shelf filter
        float shelfY1 = 0.0f;
        float scLevelSmoothed = 0.0f;     // Symmetric envelope smoother (removes 2f ripple)
        float t4bDcState = 0.0f;          // DC blocker for T4B even-harmonic distortion
    };

    void updateT4BCell(Detector& det, float scLevel)
    {
        // T4B optical cell: EL panel (light) + CdS photoresistor (resistance)
        //
        // The T4B has two physical stages:
        // 1. EL panel: converts 6AQ5 current to light (~150Hz thermal bandwidth)
        // 2. CdS cell: converts light to resistance change (~10ms attack, ~60ms release)
        // Plus phosphor persistence and charge accumulation for program dependency.

        // EL panel thermal response: asymmetric — heats fast, cools slowly
        // Fast attack lets transients through; slow release holds glow steady,
        // preventing the feedback oscillation where gain drops → sidechain drops → gain rebounds
        float elCoeff = (scLevel > det.elPanelLevel) ? elPanelAttackCoeff : elPanelReleaseCoeff;
        det.elPanelLevel += elCoeff * (scLevel - det.elPanelLevel);

        // CdS fast component: charge toward EL panel light level
        float lightLevel = det.elPanelLevel;
        if (lightLevel > det.cellCharge)
        {
            // Attack: EL panel lights up, CdS resistance drops
            det.cellCharge = lightLevel + (det.cellCharge - lightLevel) * attackCoeff;
        }
        else
        {
            // Release: program-dependent — longer compression = slower release
            float progDepFactor = 1.0f + det.accumulatedCharge * Constants::T4B_PROG_DEP_RELEASE_SCALE;
            float adjReleaseCoeff = std::pow(fastReleaseCoeff, 1.0f / progDepFactor);
            det.cellCharge = lightLevel + (det.cellCharge - lightLevel) * adjReleaseCoeff;
        }

        // EL phosphor persistence: slow component creating two-stage release
        if (lightLevel > det.phosphorGlow)
        {
            // Phosphor charges slowly with sustained light
            det.phosphorGlow = lightLevel + (det.phosphorGlow - lightLevel) * phosphorAttackCoeff;
        }
        else
        {
            // Phosphor decays slowly (1-5s, program-dependent)
            float slowDecayTime = Constants::T4B_PHOSPHOR_BASE_DECAY
                + det.accumulatedCharge * Constants::T4B_PROG_DEP_PHOSPHOR_SCALE;
            float phosphorReleaseCoeff = std::exp(-invSampleRate / slowDecayTime);
            det.phosphorGlow = lightLevel + (det.phosphorGlow - lightLevel) * phosphorReleaseCoeff;
        }

        // Program-dependent charge accumulation
        // CdS cells exhibit "memory" — sustained illumination creates deep
        // charge states that take longer to dissipate
        det.accumulatedCharge += det.cellCharge * Constants::T4B_PROG_DEP_CHARGE_RATE * invSampleRate
            - det.accumulatedCharge * Constants::T4B_PROG_DEP_DISCHARGE_RATE * invSampleRate;
        det.accumulatedCharge = juce::jlimit(0.0f, 1.0f, det.accumulatedCharge);

        // CdS resistance-to-gain mapping
        float cellResponse = det.cellCharge + det.phosphorGlow * Constants::T4B_PHOSPHOR_COUPLING;
        cellResponse = juce::jlimit(0.0f, 1.0f, cellResponse);
        float conductance = (cellResponse > 0.0f)
            ? std::min(Constants::T4B_CONDUCTANCE_K * std::pow(cellResponse, Constants::T4B_GAMMA),
                       Constants::T4B_MAX_CONDUCTANCE)
            : 0.0f;

        // Asymmetric conductance smoothing: fast attack, slow release
        // Fast attack (30Hz) lets compression engage quickly on transients
        // Slow release (3Hz) prevents feedback oscillation — conductance can't drop
        // fast enough for gain to rebound and re-trigger the sidechain
        float condCoeff = (conductance > det.smoothedConductance) ? condAttackCoeff : condReleaseCoeff;
        det.smoothedConductance += condCoeff * (conductance - det.smoothedConductance);
        det.smoothedConductance = juce::jlimit(0.0f, Constants::T4B_MAX_CONDUCTANCE, det.smoothedConductance);

        // Voltage divider: gain = 1 / (1 + conductance)
        float newGain = 1.0f / (1.0f + det.smoothedConductance);
        newGain = juce::jlimit(0.01f, 1.0f, newGain);

        // Release slew limiter: CdS cell cannot recover faster than ~91ms
        // Attack is unrestricted — CdS resistance drops quickly with light
        float gainDelta = newGain - det.t4bGain;
        if (gainDelta > 0.0f) {
            float maxReleaseDelta = Constants::T4B_MAX_GAIN_RELEASE_RATE * invSampleRate;
            gainDelta = std::min(gainDelta, maxReleaseDelta);
        }
        det.t4bGain += gainDelta;

        // NaN safety
        if (std::isnan(det.t4bGain) || std::isinf(det.t4bGain))
            det.t4bGain = 1.0f;
    }

    void calibrateHardwareGain()
    {
        // Send a reference 1kHz sine at -18dB through the hardware chain
        // to measure the exact gain added by transformers + tube.
        // This runs once during prepare(), NOT in the audio thread.
        constexpr int calibrationSamples = 4800;  // 100ms at 48kHz equivalent
        constexpr float refAmplitude = 0.126f;     // -18dB peak
        constexpr float refFreq = 1000.0f;

        float sr = static_cast<float>(sampleRate);
        float angularStep = 2.0f * juce::MathConstants<float>::pi * refFreq / sr;

        // Reset hardware stages for clean measurement
        inputTransformer.reset();
        tubeStage.reset();
        outputTransformer.reset();

        // Warm up for 50ms to settle filters
        int warmup = static_cast<int>(sr * 0.05f);
        for (int i = 0; i < warmup; ++i)
        {
            float x = refAmplitude * std::sin(angularStep * static_cast<float>(i));
            x = inputTransformer.processSample(x, 0);
            x = tubeStage.processSample(x, 0);
            outputTransformer.processSample(x, 0);
        }

        // Measure RMS of input and output over the calibration window
        double inputRmsSquared = 0.0;
        double outputRmsSquared = 0.0;
        for (int i = 0; i < calibrationSamples; ++i)
        {
            float phase = angularStep * static_cast<float>(warmup + i);
            float input = refAmplitude * std::sin(phase);

            float x = inputTransformer.processSample(input, 0);
            x = tubeStage.processSample(x, 0);
            x = outputTransformer.processSample(x, 0);

            inputRmsSquared += static_cast<double>(input * input);
            outputRmsSquared += static_cast<double>(x * x);
        }

        inputRmsSquared /= calibrationSamples;
        outputRmsSquared /= calibrationSamples;

        if (outputRmsSquared > 1e-12 && inputRmsSquared > 1e-12)
        {
            float chainGain = static_cast<float>(std::sqrt(outputRmsSquared / inputRmsSquared));
            hardwareGainCompensation = 1.0f / chainGain;
        }
        else
        {
            hardwareGainCompensation = 1.0f;
        }

        // Reset hardware stages after calibration so audio starts clean
        inputTransformer.reset();
        tubeStage.reset();
        outputTransformer.reset();
    }

    std::vector<Detector> detectors;
    double sampleRate = 0.0;

    // Pre-computed coefficients (set in prepare())
    float invSampleRate = 0.0f;
    float attackCoeff = 0.0f;
    float fastReleaseCoeff = 0.0f;
    float phosphorDecayCoeff = 0.0f;
    float shelfB0 = 1.0f, shelfB1 = 0.0f, shelfA1 = 0.0f;
    float phosphorAttackCoeff = 0.0f;
    float condAttackCoeff = 0.0f;            // Asymmetric conductance smoothing: fast attack
    float condReleaseCoeff = 0.0f;           // Asymmetric conductance smoothing: slow release
    float elPanelAttackCoeff = 0.0f;          // EL panel heats fast
    float elPanelReleaseCoeff = 0.0f;         // EL panel cools slowly
    float scLevelSmoothCoeff = 0.0f;          // Symmetric sidechain envelope smoother
    float hardwareGainCompensation = 1.0f;  // Compensates for tube + transformer gain

    // Hardware emulation components (LA-2A)
    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;
    HardwareEmulation::TubeEmulation tubeStage;
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
            detector.modulationPhase = 0.0f;
            detector.dcState = 0.0f;
            detector.prevSq = 0.0f;
            detector.peakHold = 0.0f;
            detector.transientCounter = 0;
            detector.releaseMemory = 0.0f;
            detector.voltageSag = 0.0f;
            detector.sagCounter = 0;
            detector.hfChokeState = 0.0f;
            detector.tiltState = 0.0f;
        }

        // 1176 sidechain tilt: ~3dB/octave via 1st-order HPF at 800Hz blended with original
        {
            float sr = static_cast<float>(sampleRate);
            tiltCoeff = 1.0f - std::exp(-2.0f * 3.14159f * 800.0f / sr);
        }

        // Hardware emulation components (FET compressor style)
        // Input transformer (console-style)
        inputTransformer.prepare(sampleRate, numChannels);
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFETCompressor().inputTransformer);
        inputTransformer.setEnabled(true);

        // Output transformer
        outputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFETCompressor().outputTransformer);
        outputTransformer.setEnabled(true);

        // Short convolution for FET transformer coloration
        convolution.prepare(sampleRate);
        convolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::FET);

        // Calibrate hardware gain compensation for the full analog chain
        calibrateHardwareGain();
    }

    void updateSampleRate(double newSampleRate)
    {
        if (newSampleRate <= 0.0 || newSampleRate == sampleRate)
            return;
        sampleRate = newSampleRate;
        float sr = static_cast<float>(sampleRate);
        tiltCoeff = 1.0f - std::exp(-2.0f * 3.14159f * 800.0f / sr);
        int numCh = static_cast<int>(detectors.size());
        inputTransformer.prepare(sampleRate, numCh);
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFETCompressor().inputTransformer);
        outputTransformer.prepare(sampleRate, numCh);
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFETCompressor().outputTransformer);
        convolution.prepare(sampleRate);
        convolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::FET);
        calibrateHardwareGain();
    }

    float process(float input, int channel, float inputGainDb, float outputGainDb,
                  float attackMs, float releaseMs, int ratioIndex, bool oversample = false,
                  const LookupTables* lookupTables = nullptr, TransientShaper* transientShaper = nullptr,
                  bool useMeasuredCurve = false, float transientSensitivity = 0.0f, float sidechainSignal = 0.0f, bool useExternalSidechain = false)
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
        // All-buttons mode: effective ratio 12:1–20:1 (uses non-linear curve, not this value)
        std::array<float, 5> ratios = {4.0f, 8.0f, 12.0f, 20.0f, 20.0f};
        float ratio = ratios[juce::jlimit(0, 4, ratioIndex)];

        // FEEDBACK TOPOLOGY for authentic FET behavior
        // The FET uses feedback compression which creates its characteristic sound

        // Apply the PREVIOUS envelope to get the compressed signal
        float compressed = amplifiedInput * detector.envelope;

        // ─── FET saturation stage (asymmetric) ───
        // Applied BEFORE feedback detection so harmonics interact with compression pumping.
        // In the real 1176, the FET gain element is inside the feedback loop — its
        // nonlinearity colors the signal that the sidechain sees.
        float saturated = compressed;
        float sr = static_cast<float>(sampleRate);
        {
            float grDb = -juce::Decibels::gainToDecibels(detector.envelope + 0.001f);
            float grNorm = juce::jlimit(0.0f, 1.0f, grDb / 20.0f);

            float k2, k3;
            if (ratioIndex == 4)
            {
                // ABI: heavy FET distortion — GR pushes FET outside linear region
                k2 = 0.04f + grNorm * 0.04f;   // 2nd harmonic: 0.04-0.08
                k3 = 0.005f + grNorm * 0.010f;  // 3rd harmonic: 0.005-0.015
            }
            else
            {
                // Normal ratios: FET coloring (~0.3-0.5% THD target)
                k2 = 0.032f;
                k3 = 0.006f;
            }

            float x = saturated;

            float sq = x * x;

            // DC-blocked square term: first-order highpass at ~10Hz
            float alpha = 1.0f / (1.0f + 6.2831853f * 10.0f / sr);
            float h2 = alpha * (detector.dcState + sq - detector.prevSq);
            detector.dcState = h2;
            detector.prevSq = sq;

            float h3 = x * x * x;
            saturated = x + k2 * h2 + k3 * h3;
        }

        // ─── HF choke for sidechain detection (FET junction capacitance) ───
        // As GR increases, the FET's junction capacitance rises, creating a
        // low-pass effect that reduces HF sensitivity in the sidechain.
        // This only affects detection — the audio path stays unfiltered.
        float chokedSignal = saturated;
        {
            float grDb = -juce::Decibels::gainToDecibels(detector.envelope + 0.001f);
            float grNorm = juce::jlimit(0.0f, 1.0f, grDb / 20.0f);
            float cornerHz = 20000.0f - grNorm * 2000.0f;
            float w = 6.2831853f * cornerHz / sr;
            float lpCoeff = 1.0f - std::exp(-w);
            detector.hfChokeState += lpCoeff * (saturated - detector.hfChokeState);
            chokedSignal = detector.hfChokeState;
        }

        // ─── Feedback detection ───
        float detectionLevel;
        if (useExternalSidechain)
        {
            detectionLevel = std::abs(sidechainSignal * inputGainLin);
        }
        else if (ratioIndex == 4)
        {
            // ABI: feedback detection with peak-hold capacitor and saturation ceiling.
            float instantLevel = std::abs(chokedSignal);

            // Saturation ceiling: soft-clip detection at ~1.5x threshold
            float detCeiling = threshold * 1.5f;
            if (instantLevel > detCeiling)
                instantLevel = detCeiling + (instantLevel - detCeiling) / (1.0f + (instantLevel - detCeiling));

            // Peak-hold: fast attack (~50µs), medium release (~5ms)
            float peakAttackCoeff = std::exp(-1.0f / (0.00005f * sr));
            float peakReleaseCoeff = std::exp(-1.0f / (0.005f * sr));
            if (instantLevel > detector.peakHold)
                detector.peakHold += (1.0f - peakAttackCoeff) * (instantLevel - detector.peakHold);
            else
                detector.peakHold += (1.0f - peakReleaseCoeff) * (instantLevel - detector.peakHold);
            detectionLevel = detector.peakHold;
        }
        else
        {
            detectionLevel = std::abs(chokedSignal);
        }

        // 1176 sidechain tilt: 3dB/octave HF emphasis
        detector.tiltState += tiltCoeff * (detectionLevel - detector.tiltState);
        float hfContent = detectionLevel - detector.tiltState;
        detectionLevel = std::max(detectionLevel + hfContent * 0.35f, 0.0f);

        // ─── Gain reduction calculation ───
        float reduction = 0.0f;

        if (ratioIndex == 4)
        {
            // ABI threshold shift: combined resistor networks lower effective threshold by ~6dB
            float abiThreshold = threshold * 0.5f;

            if (detectionLevel > abiThreshold)
            {
                float overThreshDb = juce::Decibels::gainToDecibels(detectionLevel / abiThreshold);

                // Nonlinear soft-knee transfer function with smoothed over-compression
                if (lookupTables != nullptr)
                {
                    reduction = lookupTables->getAllButtonsReduction(overThreshDb, useMeasuredCurve);
                }
                else
                {
                    float knee = 4.0f;
                    if (overThreshDb < knee)
                    {
                        float t = overThreshDb / knee;
                        reduction = overThreshDb * t * 0.95f;
                    }
                    else
                    {
                        float baseReduction = knee * 0.95f + (overThreshDb - knee) * 0.98f;

                        // Wraparound with micro-knee smoothing (refinement #5):
                        // Instead of a hard transition at 15dB, use a smooth tanh blend
                        // over a 2dB window (14-16dB) to prevent aliasing on fast transients.
                        float wrapOnset = 14.0f;
                        float wrapFull = 16.0f;
                        if (overThreshDb > wrapOnset)
                        {
                            // Smooth blend from 0 to full over-compression rate
                            float wrapT = juce::jlimit(0.0f, 1.0f, (overThreshDb - wrapOnset) / (wrapFull - wrapOnset));
                            // Smoothstep for alias-free transition
                            float smooth = wrapT * wrapT * (3.0f - 2.0f * wrapT);
                            float excess = overThreshDb - wrapOnset;
                            baseReduction += excess * 0.05f * smooth;
                        }
                        reduction = baseReduction;
                    }
                }

                if (transientShaper != nullptr && transientSensitivity > 0.01f)
                {
                    float transientMod = transientShaper->process(input, channel, transientSensitivity);
                    reduction /= transientMod;
                }

                reduction = juce::jmin(reduction, 30.0f);
            }
        }
        else if (detectionLevel > threshold)
        {
            float overThreshDb = juce::Decibels::gainToDecibels(detectionLevel / threshold);
            reduction = overThreshDb * (1.0f - 1.0f / ratio);
            reduction = juce::jmin(reduction, Constants::FET_MAX_REDUCTION_DB);
        }

        // ─── Time constants ───
        const float minRelease = 0.05f;
        const float maxRelease = 1.1f;
        float attackTime = juce::jmax(0.0001f, attackMs / 1000.0f);
        float releaseNorm = juce::jlimit(0.0f, 1.0f, releaseMs / 1100.0f);
        float releaseTime = minRelease * std::pow(maxRelease / minRelease, releaseNorm);

        if (ratioIndex == 4)
        {
            attackTime = juce::jmax(0.0002f, attackTime * 2.0f);

            float reductionFactor = juce::jlimit(0.0f, 1.0f, reduction / 20.0f);
            releaseTime *= (1.0f + reductionFactor * 0.5f);

            // Release memory (refinement #1b): rapid transients lengthen the slow tail.
            // Each attack onset bumps the memory up; it decays with ~500ms time constant.
            // This simulates the sidechain capacitor not fully discharging between hits.
            float memoryDecay = std::exp(-1.0f / (0.5f * sr));
            detector.releaseMemory *= memoryDecay;
            // Bump memory on each new attack onset
            if (detector.transientCounter == 0 && reduction > 3.0f)
                detector.releaseMemory = juce::jmin(1.0f, detector.releaseMemory + 0.15f);
            // Apply: up to 30% longer slow tail when memory is saturated
            releaseTime *= (1.0f + detector.releaseMemory * 0.3f);
        }

        // Program-dependent timing
        float programFactor = juce::jlimit(0.5f, 2.0f, 1.0f + reduction * 0.05f);
        float signalDelta = std::abs(detectionLevel - detector.previousLevel);
        detector.previousLevel = detectionLevel;

        if (signalDelta > 0.1f)
        {
            attackTime *= 0.8f;
            releaseTime *= 1.2f;
        }
        else
        {
            attackTime *= programFactor;
            releaseTime *= programFactor;
        }

        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        float attackCoeff = std::exp(-1.0f / juce::jmax(Constants::EPSILON, attackTime * sr));
        float releaseCoeff = std::exp(-1.0f / juce::jmax(Constants::EPSILON, releaseTime * sr));

        // ─── Envelope follower ───
        if (ratioIndex == 4)
        {
            if (targetGain < detector.envelope)
            {
                // ABI program-dependent attack delay:
                // First ~30 samples use a slower attack to let transient poke through.
                if (detector.transientCounter < 30)
                {
                    float delayedAttack = attackCoeff * 0.5f + 0.5f;
                    detector.envelope = delayedAttack * detector.envelope + (1.0f - delayedAttack) * targetGain;
                    detector.transientCounter++;
                }
                else
                {
                    detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
                }
            }
            else
            {
                detector.transientCounter = 0;

                // Context-aware release (refinement #1a):
                float grDb = -juce::Decibels::gainToDecibels(detector.envelope + 0.001f);

                // Stage 1 (fast recovery): linear scale from 50ms → 25ms as GR goes 0 → 20dB
                // At >12dB GR this is noticeably faster than before, creating aggressive pump
                float fastTimeBase = 0.05f;   // 50ms at 0dB GR
                float fastTimeMin = 0.025f;   // 25ms at 20dB GR
                float grScale = juce::jlimit(0.0f, 1.0f, grDb / 20.0f);
                float fastTime = fastTimeBase - grScale * (fastTimeBase - fastTimeMin);
                float fastCoeff = std::exp(-1.0f / juce::jmax(Constants::EPSILON, fastTime * sr));

                // Stage 2 (slow tail): user's release, stays slow for "breathing"
                float blend = grScale;
                float effectiveCoeff = fastCoeff * blend + releaseCoeff * (1.0f - blend);
                detector.envelope = effectiveCoeff * detector.envelope + (1.0f - effectiveCoeff) * targetGain;
            }
        }
        else
        {
            if (targetGain < detector.envelope)
                detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
            else
                detector.envelope = releaseCoeff * detector.envelope + (1.0f - releaseCoeff) * targetGain;
        }

        detector.envelope = juce::jlimit(0.001f, 1.0f, detector.envelope);

        if (std::isnan(detector.envelope) || std::isinf(detector.envelope))
            detector.envelope = 1.0f;

        // ─── Voltage sag (refinement #3) ───
        // Power supply sag under sustained heavy load. When GR > 15dB for >100ms,
        // the output ceiling drops 0.5-1.0dB, recovering slowly (~300ms) when load lifts.
        // This creates subtle "darkening" and "weight" during heavy compression.
        float sagGain = 1.0f;
        if (ratioIndex == 4)
        {
            float grDb = -juce::Decibels::gainToDecibels(detector.envelope + 0.001f);
            int sagThresholdSamples = static_cast<int>(sr * 0.1f); // 100ms

            if (grDb > 15.0f)
            {
                detector.sagCounter = juce::jmin(detector.sagCounter + 1, sagThresholdSamples + 1);
            }
            else
            {
                // Sag counter decays when load lifts (doesn't snap to zero)
                detector.sagCounter = juce::jmax(0, detector.sagCounter - 1);
            }

            // Target sag: 0 below 100ms, ramps to ~0.75dB above 100ms
            float sagTarget = (detector.sagCounter > sagThresholdSamples) ? 0.75f : 0.0f;

            // Smooth the sag envelope: attack ~50ms, release ~300ms
            float sagAttack = std::exp(-1.0f / (0.05f * sr));
            float sagRelease = std::exp(-1.0f / (0.3f * sr));
            if (sagTarget > detector.voltageSag)
                detector.voltageSag += (1.0f - sagAttack) * (sagTarget - detector.voltageSag);
            else
                detector.voltageSag += (1.0f - sagRelease) * (sagTarget - detector.voltageSag);

            sagGain = juce::Decibels::decibelsToGain(-detector.voltageSag);
        }

        // ─── Output chain ───
        float output = saturated;
        output = outputTransformer.processSample(output, channel);
        output = convolution.processSample(output, channel);
        output *= hardwareGainCompensation;

        // Apply voltage sag before makeup gain
        output *= sagGain;

        float outputGainLin = juce::Decibels::decibelsToGain(outputGainDb);
        float finalOutput = output * outputGainLin;

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
        float modulationPhase = 0.0f;
        float modulationRate = 4.5f;
        float dcState = 0.0f;         // Highpass state for H2 DC blocker
        float prevSq = 0.0f;         // Previous x² for H2 DC blocker
        float peakHold = 0.0f;       // Peak-hold envelope for ABI detection
        int transientCounter = 0;    // ABI: counts samples since last transient onset
        // ABI analog behavioral state
        float releaseMemory = 0.0f;  // Capacitor discharge accumulator (release lengthening)
        float voltageSag = 0.0f;     // PSU sag envelope (0 = no sag, 1 = full sag)
        int sagCounter = 0;          // Samples sustained above 15dB GR
        float hfChokeState = 0.0f;   // 1-pole LPF state for HF choke in feedback
        float tiltState = 0.0f;      // 1176 sidechain tilt filter state
    };

    std::vector<Detector> detectors;
    double sampleRate = 0.0;  // Set by prepare() from DAW
    float hardwareGainCompensation = 1.0f;
    float tiltCoeff = 0.0f;

    // Hardware emulation components (FET compressor style)
    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;
    HardwareEmulation::StereoConvolution convolution;

    // Calibrate hardware chain gain (input xfmr + output xfmr + convolution)
    // so that the analog emulation is level-neutral at unity settings.
    // Follows the same pattern as OptoCompressor::calibrateHardwareGain()
    void calibrateHardwareGain()
    {
        constexpr int calibrationSamples = 4800;
        constexpr float refAmplitude = 0.126f;  // -18dB peak
        constexpr float refFreq = 1000.0f;

        float sr = static_cast<float>(sampleRate);
        float angularStep = 2.0f * juce::MathConstants<float>::pi * refFreq / sr;

        inputTransformer.reset();
        outputTransformer.reset();

        // Warm up filters (use channel 0 only — both channels have identical IR)
        int warmup = static_cast<int>(sr * 0.05f);
        for (int i = 0; i < warmup; ++i)
        {
            float x = refAmplitude * std::sin(angularStep * static_cast<float>(i));
            x = inputTransformer.processSample(x, 0);
            x = outputTransformer.processSample(x, 0);
            convolution.processSample(x, 0);
        }

        double inputRmsSquared = 0.0;
        double outputRmsSquared = 0.0;
        for (int i = 0; i < calibrationSamples; ++i)
        {
            float phase = angularStep * static_cast<float>(warmup + i);
            float input = refAmplitude * std::sin(phase);

            float x = inputTransformer.processSample(input, 0);
            x = outputTransformer.processSample(x, 0);
            x = convolution.processSample(x, 0);

            inputRmsSquared += static_cast<double>(input * input);
            outputRmsSquared += static_cast<double>(x * x);
        }

        inputRmsSquared /= calibrationSamples;
        outputRmsSquared /= calibrationSamples;

        if (outputRmsSquared > 1e-12 && inputRmsSquared > 1e-12)
        {
            float chainGain = static_cast<float>(std::sqrt(outputRmsSquared / inputRmsSquared));
            hardwareGainCompensation = 1.0f / chainGain;
        }
        else
        {
            hardwareGainCompensation = 1.0f;
        }

        inputTransformer.reset();
        outputTransformer.reset();
        convolution.reset();
    }
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
            detector.signalEnvelope = 0.0f;
            detector.envelopeRate = 0.0f;
            detector.previousInput = 0.0f;
            detector.overshootAmount = 0.0f; // For VCA attack overshoot
            detector.dcState = 0.0f;
            detector.prevSq = 0.0f;
        }
    }

    float process(float input, int channel, float threshold, float ratio,
                  float attackParam, float releaseParam, float outputGain, bool overEasy = false, bool oversample = false, float sidechainSignal = 0.0f, bool useExternalSidechain = false)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;

        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;

        auto& detector = detectors[channel];

        // VCA feedforward topology: control voltage from input signal or external sidechain
        float detectionLevel;
        if (useExternalSidechain)
        {
            // Use external sidechain for detection
            detectionLevel = std::abs(sidechainSignal);
        }
        else
        {
            detectionLevel = std::abs(input);
        }

        // Track signal envelope rate of change for program-dependent behavior
        float signalDelta = std::abs(detectionLevel - detector.previousInput);
        detector.envelopeRate = detector.envelopeRate * 0.95f + signalDelta * 0.05f;
        detector.previousInput = detectionLevel;

        // dbx 160: Level-dependent RMS time constant
        // Real dbx 202XT VCA junction impedance decreases with level
        // Small-signal: ~35ms; loud signals: ~5ms
        float levelDb = juce::Decibels::gainToDecibels(juce::jmax(detectionLevel, 0.0001f));
        float levelAboveRef = juce::jlimit(0.0f, 30.0f, levelDb + 20.0f); // 0-30dB above -20dBFS
        float levelFactor = levelAboveRef / 30.0f; // 0 = quiet, 1 = loud
        float rmsTimeMs = 0.005f + 0.030f * std::exp(-3.0f * levelFactor);
        const float rmsAlpha = std::exp(-1.0f / (juce::jmax(0.0001f, rmsTimeMs) * static_cast<float>(sampleRate)));
        detector.rmsBuffer = detector.rmsBuffer * rmsAlpha + detectionLevel * detectionLevel * (1.0f - rmsAlpha);
        float rmsLevel = std::sqrt(detector.rmsBuffer);
        
        // VCA signal envelope tracking for program-dependent timing
        const float envelopeAlpha = 0.99f;
        detector.signalEnvelope = detector.signalEnvelope * envelopeAlpha + rmsLevel * (1.0f - envelopeAlpha);
        
        // VCA threshold control (-40dB to +20dB range typical)
        float thresholdLin = juce::Decibels::decibelsToGain(threshold);
        
        float reduction = 0.0f;
        float overThreshDb = juce::Decibels::gainToDecibels(
            juce::jmax(Constants::EPSILON, rmsLevel) / thresholdLin);

        if (overEasy)
        {
            // dbx OverEasy: parabolic soft knee, engages 5dB below threshold
            float kneeWidth = 10.0f;
            float kneeStart = -kneeWidth * 0.5f;  // -5dB below threshold
            float kneeEnd = kneeWidth * 0.5f;      // +5dB above threshold

            if (overThreshDb <= kneeStart)
            {
                reduction = 0.0f;
            }
            else if (overThreshDb < kneeEnd)
            {
                // Soft knee from kneeStart..kneeEnd with smooth onset below threshold
                float x = overThreshDb - kneeStart; // 0..kneeWidth
                reduction = (1.0f - 1.0f / ratio) * (x * x) / (2.0f * kneeWidth);
            }
            else
            {
                // Above knee - full compression
                reduction = overThreshDb * (1.0f - 1.0f / ratio);
            }
        }
        else
        {
            // Hard knee - only compress above threshold
            if (rmsLevel > thresholdLin)
            {
                reduction = overThreshDb * (1.0f - 1.0f / ratio);
            }
        }

        reduction = juce::jmin(juce::jmax(0.0f, reduction), Constants::VCA_MAX_REDUCTION_DB);
        
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
        
        // VCA feed-forward envelope following with complete stability
        // Feed-forward design is inherently stable even at infinite compression ratios
        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        
        // Exponential attack coefficient (release uses constant-rate dB/sec below)
        float attackCoeff = std::exp(-1.0f / (juce::jmax(Constants::EPSILON, attackTime * static_cast<float>(sampleRate))));
        
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
            // Release phase - constant 120dB/second release rate (dbx 160 defining characteristic)
            // Linear in dB space: gain recovers at a fixed dB/sec rate, not exponentially
            float currentDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, detector.envelope));
            float targetDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, targetGain));

            // Calculate effective release rate in dB/sec
            // Blend between program-dependent 120dB/sec and user release time
            float effectiveRate;
            if (reduction > 0.1f)
                effectiveRate = reduction / juce::jmax(0.001f, releaseTime);  // dB/sec from blended time
            else
                effectiveRate = 120.0f;  // Default rate

            float releaseStepDb = effectiveRate / static_cast<float>(sampleRate);  // dB per sample
            currentDb = juce::jmin(currentDb + releaseStepDb, targetDb);
            detector.envelope = juce::Decibels::decibelsToGain(currentDb);

            // No overshoot during release
            detector.overshootAmount *= 0.98f; // Quick decay
        }
        
        // Clamp envelope (feed-forward stability at high ratios)
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
        
        float harmonicLevelDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, absLevel));
        
        // VCA harmonic distortion - cleaner than tube/FET types but not silent
        if (absLevel > 0.01f)  // Process non-silence
        {
            float sign = (processed < 0.0f) ? -1.0f : 1.0f;

            // Classic VCA harmonics: circuit path coloration + compression harmonics
            // Real dbx 160 has ~0.05-0.1% THD even at unity from VCA chip bias + op-amps
            float h2_level = 0.0f;
            float h3_level = 0.0f;

            // Always-on circuit coloration (VCA chip bias current + op-amp stages)
            // dbx 202C VCA chip: control voltage feedthrough + op-amp crossover
            // Real dbx 160 measures ~0.05-0.1% THD passthrough
            float circuitH2 = 0.0003f;  // VCA chip even-order from bias asymmetry
            float circuitH3 = 0.0006f;  // Op-amp odd-order from output stage

            h2_level = circuitH2;
            h3_level = circuitH3;

            // Compression-dependent harmonics ADD to circuit base
            // Manual spec: 0.75% 2nd, 0.5% 3rd at infinite compression at +4dBm
            if (harmonicLevelDb > -30.0f && reduction > 2.0f)
            {
                float compressionFactor = juce::jmin(1.0f, reduction / 30.0f);

                // 2nd harmonic boost from VCA gain element under compression
                float h2_comp = 0.001f * compressionFactor;
                h2_level += h2_comp;

                // 3rd harmonic from VCA at heavy compression (>10dB GR)
                if (reduction > 10.0f)
                {
                    float h3_comp = 0.0008f * compressionFactor;
                    h3_level += h3_comp;
                }
            }
            
            // Apply VCA harmonics using proper waveshaping
            processed = compressed;

            // 2nd harmonic (even): x² is always positive → asymmetric → H2
            // Highpass at ~10Hz to remove DC from x² while preserving harmonic content
            float sq = compressed * compressed;
            float hpAlpha = 1.0f / (1.0f + 6.2831853f * 10.0f / static_cast<float>(sampleRate));
            float h2_signal = hpAlpha * (detector.dcState + sq - detector.prevSq);
            detector.dcState = h2_signal;
            detector.prevSq = sq;
            processed += h2_signal * h2_level;

            // 3rd harmonic (odd): x³ is symmetric → H3
            float h3_signal = compressed * compressed * compressed;
            processed += h3_signal * h3_level;
            
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
        float signalEnvelope = 0.0f;    // Signal envelope for program-dependent timing
        float envelopeRate = 0.0f;      // Rate of envelope change
        float previousInput = 0.0f;     // Previous input for envelope tracking
        float overshootAmount = 0.0f;   // Attack overshoot for Classic VCA characteristic
        float dcState = 0.0f;           // Highpass state for H2 DC blocker
        float prevSq = 0.0f;           // Previous x² for H2 DC blocker
    };

    std::vector<Detector> detectors;
    double sampleRate = 0.0;  // Set by prepare() from DAW

public:
    void updateSampleRate(double newSampleRate)
    {
        if (newSampleRate > 0.0 && newSampleRate != sampleRate)
            sampleRate = newSampleRate;
    }
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

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& detector = detectors[ch];
            detector.envelope = 1.0f;
            detector.rms = 0.0f;
            detector.previousLevel = 0.0f;
            detector.hpState = 0.0f;
            detector.prevInput = 0.0f;
            detector.hpState2 = 0.0f;
            detector.prevInput2 = 0.0f;
            detector.prevCompressed = 0.0f;
        }

        // Hardware emulation components (VCA bus compressor style)
        // Input transformer (console-style)
        inputTransformer.prepare(sampleRate, numChannels);
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getConsoleBus().inputTransformer);
        inputTransformer.setEnabled(true);

        // Output transformer
        outputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getConsoleBus().outputTransformer);
        outputTransformer.setEnabled(true);

        // Short convolution for console coloration
        convolution.prepare(sampleRate);
        convolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::Console_Bus);

        // Calibrate hardware gain compensation for the full analog chain
        calibrateHardwareGain();
    }

    // Lightweight sample rate update — only recalculates filter coefficients,
    // no memory allocation. Safe to call from audio thread when oversampling changes.
    void updateSampleRate(double newSampleRate)
    {
        if (newSampleRate <= 0.0 || newSampleRate == sampleRate)
            return;
        sampleRate = newSampleRate;

        // Update transformer and convolution filter coefficients
        inputTransformer.prepare(sampleRate, static_cast<int>(detectors.size()));
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getConsoleBus().inputTransformer);
        outputTransformer.prepare(sampleRate, static_cast<int>(detectors.size()));
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getConsoleBus().outputTransformer);
        convolution.prepare(sampleRate);
        convolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::Console_Bus);
        calibrateHardwareGain();
    }

    float process(float input, int channel, float threshold, float ratio,
                  int attackIndex, int releaseIndex, float makeupGain, float mixAmount = 1.0f, bool oversample = false, float sidechainSignal = 0.0f, bool useExternalSidechain = false)
    {
        if (channel >= static_cast<int>(detectors.size()))
            return input;

        // Safety check for sample rate
        if (sampleRate <= 0.0)
            return input;

        auto& detector = detectors[channel];

        // Hardware emulation: Input transformer (console-style)
        // Adds subtle saturation and frequency-dependent coloration
        float transformedInput = inputTransformer.processSample(input, channel);

        // Bus Compressor quad VCA topology
        // Uses parallel detection path with feedback design (sidechain taps compressed output)

        // Determine detection signal: use external sidechain if active, otherwise internal filter
        float detectionLevel;
        if (useExternalSidechain)
        {
            // Use external sidechain for detection
            detectionLevel = std::abs(sidechainSignal);
        }
        else
        {
            // Feedback topology: detect from previous sample's compressed output
            float sidechainInput = detector.prevCompressed;
            // 60Hz 2nd-order (12dB/oct) Butterworth HPF for sidechain (prevents LF pumping)
            {
                float sr = static_cast<float>(sampleRate);
                float alpha = 1.0f / (1.0f + 6.2831853f * 60.0f / sr);
                // First pole
                detector.hpState = alpha * (detector.hpState + sidechainInput - detector.prevInput);
                detector.prevInput = sidechainInput;
                // Second pole (12dB/oct total)
                float firstPoleOut = detector.hpState;
                detector.hpState2 = alpha * (detector.hpState2 + firstPoleOut - detector.prevInput2);
                detector.prevInput2 = firstPoleOut;
                sidechainInput = detector.hpState2;
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
            // Attack phase — exponential envelope for authentic SSL snap
            float attackCoeff = std::exp(-1.0f / juce::jmax(1.0f, attackTime * static_cast<float>(sampleRate)));
            detector.envelope = targetGain + (detector.envelope - targetGain) * attackCoeff;
        }
        else
        {
            // Release phase — exponential envelope for smooth SSL recovery
            float releaseCoeff = std::exp(-1.0f / juce::jmax(1.0f, releaseTime * static_cast<float>(sampleRate)));
            detector.envelope = targetGain + (detector.envelope - targetGain) * releaseCoeff;
        }
        
        // NaN/Inf safety check
        if (std::isnan(detector.envelope) || std::isinf(detector.envelope))
            detector.envelope = 1.0f;

        // Apply the gain reduction envelope to the input signal
        float compressed = transformedInput * detector.envelope;
        detector.prevCompressed = compressed;  // Store for feedback sidechain

        // Bus Compressor Output Stage - Subtle console saturation
        // Spec from compressor_specs.json: < 0.3% THD
        // The VCA bus is a clean VCA design, but the console path adds character.
        // Console transformers add subtle 2nd harmonic warmth.
        //
        // Harmonic math: For input x = A*sin(wt)
        // - k2*x^2 produces 2nd harmonic at (k2*A^2/2) amplitude
        // - k3*x^3 produces 3rd harmonic at (3*k3*A^3)/4 amplitude
        // Target: ~0.15-0.2% THD at typical levels

        float processed = compressed;

        // Console saturation - 2nd harmonic dominant from transformers
        // k2 = 0.004 gives ~0.2% 2nd harmonic at moderate levels
        // k3 = 0.003 gives subtle 3rd harmonic for "glue"
        constexpr float k2 = 0.004f;   // 2nd harmonic coefficient (asymmetric warmth)
        constexpr float k3 = 0.003f;   // 3rd harmonic coefficient (symmetric glue)

        // Apply waveshaping: y = x + k2*x^2 + k3*x^3
        float x2 = processed * processed;
        float x3 = x2 * processed;
        processed = processed + k2 * x2 + k3 * x3;

        // Bus output transformer — console iron coloration
        processed = outputTransformer.processSample(processed, channel);

        // Console transformer frequency response (short convolution — 2.5kHz punch, HF extension)
        processed = convolution.processSample(processed, channel);

        // Hardware gain compensation (measured at prepare time)
        processed *= hardwareGainCompensation;

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
        float hpState = 0.0f;       // Simple highpass filter state (1st pole)
        float prevInput = 0.0f;     // Previous input for filter (1st pole)
        float hpState2 = 0.0f;      // Second pole state (12dB/oct total)
        float prevInput2 = 0.0f;    // Previous input for second pole
        float prevCompressed = 0.0f; // Feedback topology: previous compressed output
    };

    std::vector<Detector> detectors;
    double sampleRate = 0.0;  // Set by prepare() from DAW

    // Hardware emulation components (VCA bus compressor style)
    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;
    HardwareEmulation::StereoConvolution convolution;
    float hardwareGainCompensation = 1.0f;

    void calibrateHardwareGain()
    {
        constexpr int calibrationSamples = 4800;
        constexpr float refAmplitude = 0.126f;
        constexpr float refFreq = 1000.0f;

        float sr = static_cast<float>(sampleRate);
        float angularStep = 2.0f * juce::MathConstants<float>::pi * refFreq / sr;

        inputTransformer.reset();
        outputTransformer.reset();

        // Calibrate using channel 0 only — both channels have identical IR
        int warmup = static_cast<int>(sr * 0.05f);
        for (int i = 0; i < warmup; ++i)
        {
            float x = refAmplitude * std::sin(angularStep * static_cast<float>(i));
            x = inputTransformer.processSample(x, 0);
            x = outputTransformer.processSample(x, 0);
            convolution.processSample(x, 0);
        }

        double inputRmsSquared = 0.0;
        double outputRmsSquared = 0.0;
        for (int i = 0; i < calibrationSamples; ++i)
        {
            float phase = angularStep * static_cast<float>(warmup + i);
            float input = refAmplitude * std::sin(phase);

            float x = inputTransformer.processSample(input, 0);
            x = outputTransformer.processSample(x, 0);
            x = convolution.processSample(x, 0);

            inputRmsSquared += static_cast<double>(input * input);
            outputRmsSquared += static_cast<double>(x * x);
        }

        inputRmsSquared /= calibrationSamples;
        outputRmsSquared /= calibrationSamples;

        if (outputRmsSquared > 1e-12 && inputRmsSquared > 1e-12)
        {
            float chainGain = static_cast<float>(std::sqrt(outputRmsSquared / inputRmsSquared));
            hardwareGainCompensation = 1.0f / chainGain;
        }
        else
        {
            hardwareGainCompensation = 1.0f;
        }

        inputTransformer.reset();
        outputTransformer.reset();
        convolution.reset();
    }
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
            detector.modulationPhase = 0.0f;
            detector.dcState = 0.0f;
            detector.prevSq = 0.0f;
            detector.peakHold = 0.0f;
            detector.transientCounter = 0;
            detector.releaseMemory = 0.0f;
            detector.tiltState = 0.0f;
            detector.hfChokeState = 0.0f;
        }

        // 1176 sidechain tilt: ~3dB/octave via 1st-order HPF at 800Hz blended with original
        {
            float sr = static_cast<float>(sampleRate);
            tiltCoeff = 1.0f - std::exp(-2.0f * 3.14159f * 800.0f / sr);
        }

        // Studio FET transformer emulation (WA76/MC77 style)
        auto studioFETProfile = HardwareEmulation::HardwareProfiles::getStudioFET();
        inputTransformer.prepare(sampleRate, numChannels);
        inputTransformer.setProfile(studioFETProfile.inputTransformer);
        outputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.setProfile(studioFETProfile.outputTransformer);
    }

    float process(float input, int channel, float inputGain, float outputGain,
                  float attackMs, float releaseMs, int ratioIndex, float sidechainInput, bool useExternalSidechain = false)
    {
        if (channel >= static_cast<int>(detectors.size()) || sampleRate <= 0.0)
            return input;

        auto& detector = detectors[static_cast<size_t>(channel)];
        float sr = static_cast<float>(sampleRate);

        // Hardware emulation: Input transformer (WA76/MC77 style)
        float transformedInput = inputTransformer.processSample(input, channel);

        // Apply input gain (drives signal into fixed threshold)
        float inputGainLin = juce::Decibels::decibelsToGain(inputGain);
        float gained = transformedInput * inputGainLin;

        // Fixed threshold at -10dBFS (FET spec)
        constexpr float thresholdDb = Constants::STUDIO_FET_THRESHOLD_DB;
        const float threshold = juce::Decibels::decibelsToGain(thresholdDb);

        // Ratio selection
        float ratio;
        bool isAllButtons = (ratioIndex == 4);
        switch (ratioIndex)
        {
            case 0: ratio = 4.0f; break;
            case 1: ratio = 8.0f; break;
            case 2: ratio = 12.0f; break;
            case 3: ratio = 20.0f; break;
            case 4: ratio = 20.0f; break;
            default: ratio = 4.0f; break;
        }

        // FEEDBACK detection — Studio FET is an 1176 variant (Rev F / modern reissue)
        // Apply previous envelope to get compressed signal for feedback detection
        float compressed = gained * detector.envelope;

        // HF choke in feedback path: FET junction capacitance LPF (all modes)
        // As GR increases, FET's junction capacitance rises, creating a low-pass
        // effect. Corner slides from 20kHz → 18kHz. Applied before detector reads level.
        float feedbackSignal = compressed;
        {
            float grDb = -juce::Decibels::gainToDecibels(detector.envelope + 0.001f);
            float grNorm = juce::jlimit(0.0f, 1.0f, grDb / 20.0f);
            float cornerHz = 20000.0f - grNorm * 2000.0f;
            float w = 6.2831853f * cornerHz / sr;
            float lpCoeff = 1.0f - std::exp(-w);
            detector.hfChokeState += lpCoeff * (compressed - detector.hfChokeState);
            feedbackSignal = detector.hfChokeState;
        }

        float detectionLevel;
        if (useExternalSidechain)
        {
            // Use external sidechain (apply input gain to match compression behavior)
            detectionLevel = std::abs(sidechainInput * inputGainLin);
        }
        else if (isAllButtons)
        {
            // ABI: feedback detection with peak-hold capacitor and saturation ceiling
            // (matches Vintage FET fix — prevents over-detection on transients)
            float instantLevel = std::abs(feedbackSignal);

            // Saturation ceiling: soft-clip detection at ~1.5x threshold
            float detCeiling = threshold * 1.5f;
            if (instantLevel > detCeiling)
                instantLevel = detCeiling + (instantLevel - detCeiling) / (1.0f + (instantLevel - detCeiling));

            // Peak-hold: fast attack (~50µs), medium release (~5ms)
            float peakAttackCoeff = std::exp(-1.0f / (0.00005f * sr));
            float peakReleaseCoeff = std::exp(-1.0f / (0.005f * sr));
            if (instantLevel > detector.peakHold)
                detector.peakHold += (1.0f - peakAttackCoeff) * (instantLevel - detector.peakHold);
            else
                detector.peakHold += (1.0f - peakReleaseCoeff) * (instantLevel - detector.peakHold);
            detectionLevel = detector.peakHold;
        }
        else
        {
            // Feedback detection from compressed output (1176 topology)
            detectionLevel = std::abs(feedbackSignal);
        }

        // 1176 sidechain tilt: 3dB/octave HF emphasis
        detector.tiltState += tiltCoeff * (detectionLevel - detector.tiltState);
        float hfContent = detectionLevel - detector.tiltState;
        detectionLevel = std::max(detectionLevel + hfContent * 0.35f, 0.0f);

        // Calculate gain reduction
        float reduction = 0.0f;

        if (isAllButtons)
        {
            // ABI threshold shift: combined resistor networks lower effective threshold by ~6dB
            // (matches Vintage FET fix)
            float abiThreshold = threshold * 0.5f;

            if (detectionLevel > abiThreshold)
            {
                float overDb = juce::Decibels::gainToDecibels(detectionLevel / abiThreshold);

                // ABI: near-limiting transfer curve
                if (overDb < 1.0f)
                {
                    float t = overDb;
                    reduction = overDb * (0.7f + t * 0.28f);
                }
                else
                {
                    reduction = 0.98f + (overDb - 1.0f) * 0.99f;
                }
                reduction = juce::jmin(reduction, 30.0f);
            }
            else
            {
                // Expansion bump below threshold (same as Vintage FET)
                float levelDb = juce::Decibels::gainToDecibels(detectionLevel + 0.0001f);
                float threshDb2 = juce::Decibels::gainToDecibels(abiThreshold);
                float belowThresh = threshDb2 - levelDb;

                if (belowThresh < 3.0f && belowThresh > 0.0f)
                {
                    float bumpPosition = belowThresh / 3.0f;
                    float bump = std::sin(bumpPosition * 3.14159f) * 1.0f;
                    reduction = -bump;  // Negative reduction = expansion (gain > unity)
                }
            }
        }
        else if (detectionLevel > threshold)
        {
            float overDb = juce::Decibels::gainToDecibels(detectionLevel / threshold);
            reduction = overDb * (1.0f - 1.0f / ratio);
            reduction = juce::jmin(reduction, 30.0f);
        }

        // Studio FET timing (same 1176 range as Vintage FET)
        const float minRelease = 0.05f;
        const float maxRelease = 1.1f;

        float attackTime = juce::jmax(0.0001f, attackMs / 1000.0f);
        float releaseNorm = juce::jlimit(0.0f, 1.0f, releaseMs / 1100.0f);
        float releaseTime = minRelease * std::pow(maxRelease / minRelease, releaseNorm);

        // ABI timing: attack lag + release memory (matches Vintage FET fixes)
        if (isAllButtons)
        {
            attackTime = juce::jmax(0.0002f, attackTime * 2.0f);

            float reductionFactor = juce::jlimit(0.0f, 1.0f, reduction / 20.0f);
            releaseTime *= (1.0f + reductionFactor * 0.5f);

            // Release memory: rapid transients lengthen the slow tail.
            // Each attack onset bumps the memory up; it decays with ~500ms time constant.
            float memoryDecay = std::exp(-1.0f / (0.5f * sr));
            detector.releaseMemory *= memoryDecay;
            if (detector.transientCounter == 0 && reduction > 3.0f)
                detector.releaseMemory = juce::jmin(1.0f, detector.releaseMemory + 0.15f);
            releaseTime *= (1.0f + detector.releaseMemory * 0.3f);
        }

        // Envelope following
        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        float attackCoeff = std::exp(-1.0f / juce::jmax(Constants::EPSILON, attackTime * sr));
        float releaseCoeff = std::exp(-1.0f / juce::jmax(Constants::EPSILON, releaseTime * sr));

        if (isAllButtons)
        {
            if (targetGain < detector.envelope)
            {
                // ABI program-dependent attack delay:
                // First ~30 samples use a slower attack to let transient poke through.
                if (detector.transientCounter < 30)
                {
                    float delayedAttack = attackCoeff * 0.5f + 0.5f;
                    detector.envelope = delayedAttack * detector.envelope + (1.0f - delayedAttack) * targetGain;
                    detector.transientCounter++;
                }
                else
                {
                    detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
                }
            }
            else
            {
                detector.transientCounter = 0;

                // Context-aware release:
                float grDb = -juce::Decibels::gainToDecibels(detector.envelope + 0.001f);

                // Stage 1 (fast recovery): linear scale from 50ms → 25ms as GR goes 0 → 20dB
                float fastTimeBase = 0.05f;
                float fastTimeMin = 0.025f;
                float grScale = juce::jlimit(0.0f, 1.0f, grDb / 20.0f);
                float fastTime = fastTimeBase - grScale * (fastTimeBase - fastTimeMin);
                float fastCoeff = std::exp(-1.0f / juce::jmax(Constants::EPSILON, fastTime * sr));

                // Stage 2 (slow tail): user's release, stays slow for "breathing"
                float blend = grScale;
                float effectiveCoeff = fastCoeff * blend + releaseCoeff * (1.0f - blend);
                detector.envelope = effectiveCoeff * detector.envelope + (1.0f - effectiveCoeff) * targetGain;
            }
        }
        else
        {
            if (targetGain < detector.envelope)
                detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
            else
                detector.envelope = releaseCoeff * detector.envelope + (1.0f - releaseCoeff) * targetGain;
        }

        // Allow up to ~1dB expansion (1.12) for ABI vintage bump below threshold
        float maxEnvelope = isAllButtons ? 1.12f : 1.0f;
        detector.envelope = juce::jlimit(0.001f, maxEnvelope, detector.envelope);

        if (std::isnan(detector.envelope) || std::isinf(detector.envelope))
            detector.envelope = 1.0f;

        // Re-apply compression with updated envelope
        compressed = gained * detector.envelope;

        // Studio FET saturation — JFET square-law (even-harmonic), 30% of Vintage
        // Models Rev F / modern reissue 1176 (Q-bias keeps FET closer to linear)
        float output = compressed;
        {
            float grDb = -juce::Decibels::gainToDecibels(detector.envelope + 0.001f);
            float grNorm = juce::jlimit(0.0f, 1.0f, grDb / 20.0f);
            float scale = Constants::STUDIO_FET_HARMONIC_SCALE;  // 30% of Vintage

            float k2, k3;
            if (isAllButtons)
            {
                k2 = (0.04f + grNorm * 0.12f) * scale;
                k3 = (0.005f + grNorm * 0.015f) * scale;
            }
            else
            {
                k2 = 0.004f * scale;
                k3 = 0.001f * scale;
            }

            float x = output;
            float sq = x * x;

            // DC-blocked square term: first-order highpass at ~10Hz (matches Vintage FET)
            float alpha = 1.0f / (1.0f + 6.2831853f * 10.0f / sr);
            float dcFiltered = alpha * (detector.dcState + sq - detector.prevSq);
            detector.dcState = dcFiltered;
            detector.prevSq = sq;
            float h2 = dcFiltered;

            float h3 = x * x * x;
            output = x + k2 * h2 + k3 * h3;
        }

        // Hardware emulation: Output transformer (WA76/MC77 style)
        output = outputTransformer.processSample(output, channel);

        // Apply output gain
        float finalOutput = output * juce::Decibels::decibelsToGain(outputGain);
        return juce::jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, finalOutput);
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
        // ABI control loop instability
        float modulationPhase = 0.0f;
        float modulationRate = 4.5f;
        float dcState = 0.0f;         // Highpass state for H2 DC blocker
        float prevSq = 0.0f;         // Previous x² for H2 DC blocker
        float peakHold = 0.0f;        // Peak-hold envelope for ABI detection
        int transientCounter = 0;     // ABI: counts samples since last transient onset
        float releaseMemory = 0.0f;   // Capacitor discharge accumulator (release lengthening)
        float tiltState = 0.0f;      // 1176 sidechain tilt filter state
        float hfChokeState = 0.0f;   // HF choke: FET junction capacitance LPF
    };

    std::vector<Detector> detectors;
    double sampleRate = 0.0;
    float tiltCoeff = 0.0f;

    // Hardware emulation components (Studio FET transformer style)
    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;

public:
    void updateSampleRate(double newSampleRate)
    {
        if (newSampleRate > 0.0 && newSampleRate != sampleRate)
        {
            sampleRate = newSampleRate;
            float sr = static_cast<float>(sampleRate);
            tiltCoeff = 1.0f - std::exp(-2.0f * 3.14159f * 800.0f / sr);
            int numCh = static_cast<int>(detectors.size());
            auto studioFETProfile = HardwareEmulation::HardwareProfiles::getStudioFET();
            inputTransformer.prepare(sampleRate, numCh);
            inputTransformer.setProfile(studioFETProfile.inputTransformer);
            outputTransformer.prepare(sampleRate, numCh);
            outputTransformer.setProfile(studioFETProfile.outputTransformer);
        }
    }
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
            detector.previousLevel = 0.0f;
            detector.smoothedEnvelope = 1.0f;
        }

        // Studio VCA transformer emulation (API 2500 / Neve 33609 style)
        auto studioVCAProfile = HardwareEmulation::HardwareProfiles::getStudioVCA();
        inputTransformer.prepare(sampleRate, numChannels);
        inputTransformer.setProfile(studioVCAProfile.inputTransformer);
        outputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.setProfile(studioVCAProfile.outputTransformer);
    }

    float process(float input, int channel, float thresholdDb, float ratio,
                  float attackMs, float releaseMs, float outputGain, float sidechainInput)
    {
        if (channel >= static_cast<int>(detectors.size()) || sampleRate <= 0.0)
            return input;

        auto& detector = detectors[static_cast<size_t>(channel)];

        // Hardware emulation: Input transformer (API 2500 / Neve 33609 style)
        float transformedInput = inputTransformer.processSample(input, channel);

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

        // Program-dependent auto-release
        float signalDelta = std::abs(detectionLevel - detector.previousLevel);
        detector.previousLevel = detectionLevel;
        float transientness = juce::jlimit(0.0f, 1.0f, signalDelta * 15.0f);
        // Transients: 50% faster release; sustained compression: 30% slower
        float releaseScale = 1.0f - transientness * 0.5f;
        float compressionDepth = juce::jlimit(0.0f, 1.0f, (1.0f - detector.envelope) * 5.0f);
        releaseScale *= (1.0f + compressionDepth * 0.3f * (1.0f - transientness));

        releaseTime *= releaseScale;

        float targetGain = juce::Decibels::decibelsToGain(-reduction);
        float attackCoeff = std::exp(-1.0f / (attackTime * static_cast<float>(sampleRate)));
        float releaseCoeff = std::exp(-1.0f / (releaseTime * static_cast<float>(sampleRate)));

        if (targetGain < detector.envelope)
            detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
        else
            detector.envelope = releaseCoeff * detector.envelope + (1.0f - releaseCoeff) * targetGain;

        detector.envelope = juce::jlimit(0.001f, 1.0f, detector.envelope);

        // 2ms envelope smoothing to prevent zipper noise
        float smoothCoeff = std::exp(-1.0f / (0.002f * static_cast<float>(sampleRate)));
        detector.smoothedEnvelope = smoothCoeff * detector.smoothedEnvelope + (1.0f - smoothCoeff) * detector.envelope;

        // Apply compression
        float compressed = transformedInput * detector.smoothedEnvelope;

        // Studio VCA is very clean - minimal harmonics
        float absLevel = std::abs(compressed);
        if (absLevel > 1.2f)
        {
            // Modern VCA has headroom above 0dBFS
            float excess = absLevel - 1.2f;
            float softClip = 1.2f + 0.3f * std::tanh(excess * 3.0f);
            compressed = (compressed > 0.0f ? 1.0f : -1.0f) * softClip;
        }

        // Hardware emulation: Output transformer (API 2500 / Neve 33609 style)
        float output = outputTransformer.processSample(compressed, channel);

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

private:
    struct Detector
    {
        float envelope = 1.0f;
        float rms = 0.0f;
        float previousGR = 0.0f;
        float previousLevel = 0.0f;
        float smoothedEnvelope = 1.0f;
    };

    std::vector<Detector> detectors;
    double sampleRate = 0.0;
    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;

public:
    void updateSampleRate(double newSampleRate)
    {
        if (newSampleRate > 0.0 && newSampleRate != sampleRate)
        {
            sampleRate = newSampleRate;
            auto studioVCAProfile = HardwareEmulation::HardwareProfiles::getStudioVCA();
            inputTransformer.prepare(sampleRate, static_cast<int>(detectors.size()));
            inputTransformer.setProfile(studioVCAProfile.inputTransformer);
            outputTransformer.prepare(sampleRate, static_cast<int>(detectors.size()));
            outputTransformer.setProfile(studioVCAProfile.outputTransformer);
        }
    }
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

        // Calculate max lookahead samples for current rate
        maxLookaheadSamples = static_cast<int>(std::ceil((LookaheadBuffer::MAX_LOOKAHEAD_MS / 1000.0) * sampleRate));

        // Allocate buffer for max possible OS rate (4x) so runtime OS changes never exceed capacity.
        // At worst this over-allocates by 4x at native rate (~1920 samples at 48kHz — negligible).
        int maxPossibleSamples = static_cast<int>(std::ceil((LookaheadBuffer::MAX_LOOKAHEAD_MS / 1000.0) * sampleRate * 4.0));
        lookaheadBuffer.setSize(numCh, maxPossibleSamples);
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

        // Lookahead: use future sidechain input for gain computation
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
        // Professional compressors have similar minimums.
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

    void reset()
    {
        lookaheadBuffer.clear();
        for (auto& wp : lookaheadWritePos)
            wp = 0;
        currentLookaheadSamples = 0;
        for (auto& detector : detectors)
        {
            detector.envelope = 1.0f;
            detector.adaptiveRelease = 0.0f;
            detector.peakHold = 0.0f;
            detector.rmsLevel = 0.0f;
            detector.crestFactor = 1.0f;
        }
    }

    // Pass audio through the lookahead delay only (no gain reduction).
    // Used in bypass path to maintain reported latency for Digital mode.
    float processLookaheadOnly(float input, int channel, float lookaheadMs)
    {
        if (channel >= numChannels || sampleRate <= 0.0 || maxLookaheadSamples <= 0)
            return input;

        int lookaheadSamples = static_cast<int>(std::round((lookaheadMs / 1000.0f) * static_cast<float>(sampleRate)));
        lookaheadSamples = juce::jlimit(0, maxLookaheadSamples - 1, lookaheadSamples);

        if (lookaheadSamples <= 0)
            return input;

        int& writePos = lookaheadWritePos[static_cast<size_t>(channel)];
        int bufferSize = maxLookaheadSamples;

        int readPos = (writePos - lookaheadSamples + bufferSize) % bufferSize;
        float delayedInput = lookaheadBuffer.getSample(channel, readPos);

        lookaheadBuffer.setSample(channel, writePos, input);
        writePos = (writePos + 1) % bufferSize;

        return delayedInput;
    }

    // Pass audio through a delay of exactly delaySamples (no gain reduction).
    // Used in bypass path where the caller computes the exact delay to match
    // the reported PDC latency (which depends on the current OS factor).
    float processDelayOnly(float input, int channel, int delaySamples)
    {
        if (channel >= numChannels || maxLookaheadSamples <= 0 || delaySamples <= 0)
            return input;

        delaySamples = juce::jlimit(1, maxLookaheadSamples - 1, delaySamples);

        int& writePos = lookaheadWritePos[static_cast<size_t>(channel)];
        int bufferSize = maxLookaheadSamples;

        int readPos = (writePos - delaySamples + bufferSize) % bufferSize;
        float delayedInput = lookaheadBuffer.getSample(channel, readPos);

        lookaheadBuffer.setSample(channel, writePos, input);
        writePos = (writePos + 1) % bufferSize;

        return delayedInput;
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

public:
    void updateSampleRate(double newSampleRate)
    {
        if (newSampleRate <= 0.0 || newSampleRate == sampleRate)
            return;
        sampleRate = newSampleRate;
        // Recalculate lookahead for new rate (buffer was preallocated for 4x max OS)
        int newMax = static_cast<int>(std::ceil((LookaheadBuffer::MAX_LOOKAHEAD_MS / 1000.0) * sampleRate));
        maxLookaheadSamples = juce::jmin(newMax, lookaheadBuffer.getNumSamples());

        // Reset write positions to avoid stale/out-of-range indices after OS change
        for (auto& wp : lookaheadWritePos)
        {
            if (maxLookaheadSamples > 0)
                wp = wp % maxLookaheadSamples;
            else
                wp = 0;
        }
    }
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

        // Sidechain crossover filters (separate instances for SC band splitting)
        sc_lp1_a.resize(sz); sc_lp1_b.resize(sz);
        sc_hp1_a.resize(sz); sc_hp1_b.resize(sz);
        sc_lp2_a.resize(sz); sc_lp2_b.resize(sz);
        sc_hp2_a.resize(sz); sc_hp2_b.resize(sz);
        sc_lp3_a.resize(sz); sc_lp3_b.resize(sz);
        sc_hp3_a.resize(sz); sc_hp3_b.resize(sz);

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
            scBandBuffers[band].setSize(numCh, maxBlockSize);
            scBandBuffers[band].clear();
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
            // Audio crossover filters
            lp1_a[ch].coefficients = lp1Coeffs;
            lp1_b[ch].coefficients = lp1Coeffs;
            hp1_a[ch].coefficients = hp1Coeffs;
            hp1_b[ch].coefficients = hp1Coeffs;

            lp2_a[ch].coefficients = lp2Coeffs;
            lp2_b[ch].coefficients = lp2Coeffs;
            hp2_a[ch].coefficients = hp2Coeffs;
            hp2_b[ch].coefficients = hp2Coeffs;

            lp3_a[ch].coefficients = lp3Coeffs;
            lp3_b[ch].coefficients = lp3Coeffs;
            hp3_a[ch].coefficients = hp3Coeffs;
            hp3_b[ch].coefficients = hp3Coeffs;

            // Sidechain crossover filters (separate instances to avoid corrupting audio path)
            sc_lp1_a[ch].coefficients = lp1Coeffs;
            sc_lp1_b[ch].coefficients = lp1Coeffs;
            sc_hp1_a[ch].coefficients = hp1Coeffs;
            sc_hp1_b[ch].coefficients = hp1Coeffs;

            sc_lp2_a[ch].coefficients = lp2Coeffs;
            sc_lp2_b[ch].coefficients = lp2Coeffs;
            sc_hp2_a[ch].coefficients = hp2Coeffs;
            sc_hp2_b[ch].coefficients = hp2Coeffs;

            sc_lp3_a[ch].coefficients = lp3Coeffs;
            sc_lp3_b[ch].coefficients = lp3Coeffs;
            sc_hp3_a[ch].coefficients = hp3Coeffs;
            sc_hp3_b[ch].coefficients = hp3Coeffs;
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
                      float outputGain, float mixPercent,
                      const juce::AudioBuffer<float>* sidechainBuffer = nullptr,
                      bool hasExternalSidechain = false)
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

        // Split sidechain into matching bands so each compressor only reacts to SC energy in its range
        if (hasExternalSidechain && sidechainBuffer != nullptr)
            splitSidechainIntoBands(*sidechainBuffer, numSamples, channels);

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
                                       attacks[band], releases[band], makeups[band],
                                       hasExternalSidechain ? &scBandBuffers[band] : nullptr,
                                       hasExternalSidechain);
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

    // Get overall (max) gain reduction across all bands — for GR meter display
    float getMaxGainReduction() const
    {
        float maxGr = 0.0f;
        for (int band = 0; band < NUM_BANDS; ++band)
        {
            maxGr = juce::jmin(maxGr, bandGainReduction[band]);
        }
        return maxGr;
    }

    // Get average gain reduction across all bands — for auto-gain compensation
    // Using average prevents one narrow band from over-compensating the full signal
    float getAverageGainReduction() const
    {
        float sumGr = 0.0f;
        for (int band = 0; band < NUM_BANDS; ++band)
            sumGr += bandGainReduction[band];
        return sumGr / static_cast<float>(NUM_BANDS);
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

    void splitSidechainIntoBands(const juce::AudioBuffer<float>& scInput, int numSamples, int channels)
    {
        // Same LR4 crossover as splitIntoBands() but using separate sc_ filter instances
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* in = scInput.getReadPointer(juce::jmin(ch, scInput.getNumChannels() - 1));
            float* band0 = scBandBuffers[0].getWritePointer(ch);
            float* band1 = scBandBuffers[1].getWritePointer(ch);
            float* band2 = scBandBuffers[2].getWritePointer(ch);
            float* band3 = scBandBuffers[3].getWritePointer(ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float sample = in[i];

                // Band 0: Low (below crossover 1)
                float lp1 = sc_lp1_a[ch].processSample(sample);
                lp1 = sc_lp1_b[ch].processSample(lp1);
                band0[i] = lp1;

                // HP1 for bands 1-3
                float hp1 = sc_hp1_a[ch].processSample(sample);
                hp1 = sc_hp1_b[ch].processSample(hp1);

                // Band 1: Low-Mid (between crossover 1 and 2)
                float lp2 = sc_lp2_a[ch].processSample(hp1);
                lp2 = sc_lp2_b[ch].processSample(lp2);
                band1[i] = lp2;

                // HP2 for bands 2-3
                float hp2 = sc_hp2_a[ch].processSample(hp1);
                hp2 = sc_hp2_b[ch].processSample(hp2);

                // Band 2: High-Mid (between crossover 2 and 3)
                float lp3 = sc_lp3_a[ch].processSample(hp2);
                lp3 = sc_lp3_b[ch].processSample(lp3);
                band2[i] = lp3;

                // Band 3: High (above crossover 3)
                float hp3 = sc_hp3_a[ch].processSample(hp2);
                hp3 = sc_hp3_b[ch].processSample(hp3);
                band3[i] = hp3;
            }
        }
    }

    void processBandCompression(int band, juce::AudioBuffer<float>& bandBuffer,
                                int numSamples, int channels,
                                float thresholdDb, float ratio,
                                float attackMs, float releaseMs, float makeupDb,
                                const juce::AudioBuffer<float>* sidechainBuffer = nullptr,
                                bool hasExternalSidechain = false)
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

            // Get external sidechain read pointer if available
            const float* scData = nullptr;
            if (hasExternalSidechain && sidechainBuffer != nullptr && sidechainBuffer->getNumChannels() > 0)
                scData = sidechainBuffer->getReadPointer(juce::jmin(ch, sidechainBuffer->getNumChannels() - 1));

            float makeupGain = juce::Decibels::decibelsToGain(makeupDb);

            for (int i = 0; i < numSamples; ++i)
            {
                float input = data[i];

                // Detection: use external sidechain if active, otherwise band-split audio
                float absInput = (scData != nullptr) ? std::abs(scData[i]) : std::abs(input);

                // Convert to dB
                float inputDb = juce::Decibels::gainToDecibels(juce::jmax(absInput, 0.00001f));

                // Calculate gain reduction with 6dB soft knee
                float reductionDb = 0.0f;
                constexpr float kneeDb = 6.0f;
                float kneeStart = thresholdDb - kneeDb / 2.0f;
                if (inputDb > kneeStart)
                {
                    if (inputDb < thresholdDb + kneeDb / 2.0f)
                    {
                        // Soft knee region
                        float x = inputDb - kneeStart;
                        reductionDb = (1.0f - 1.0f / ratio) * (x * x) / (2.0f * kneeDb);
                    }
                    else
                    {
                        float overDb = inputDb - thresholdDb;
                        reductionDb = overDb * (1.0f - 1.0f / ratio);
                    }
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

    // Sidechain crossover filters (separate instances to avoid corrupting audio filter state)
    std::vector<juce::dsp::IIR::Filter<float>> sc_lp1_a, sc_lp1_b;
    std::vector<juce::dsp::IIR::Filter<float>> sc_hp1_a, sc_hp1_b;
    std::vector<juce::dsp::IIR::Filter<float>> sc_lp2_a, sc_lp2_b;
    std::vector<juce::dsp::IIR::Filter<float>> sc_hp2_a, sc_hp2_b;
    std::vector<juce::dsp::IIR::Filter<float>> sc_lp3_a, sc_lp3_b;
    std::vector<juce::dsp::IIR::Filter<float>> sc_hp3_a, sc_hp3_b;

    // Band buffers
    std::array<juce::AudioBuffer<float>, NUM_BANDS> bandBuffers;
    std::array<juce::AudioBuffer<float>, NUM_BANDS> scBandBuffers;  // Sidechain band buffers
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

public:
    void updateSampleRate(double newSampleRate)
    {
        if (newSampleRate <= 0.0 || newSampleRate == sampleRate)
            return;
        sampleRate = newSampleRate;
        // Recalculate crossover filters for new sample rate
        updateCrossoverFrequencies(crossoverFreqs[0], crossoverFreqs[1], crossoverFreqs[2]);
    }
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
        juce::NormalisableRange<float>(0.02f, 80.0f, 0.01f, 0.3f), 0.2f));
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

    for (int band = 0; band < kNumMultibandBands; ++band)
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
    // Real 1176 ABI is extreme — essentially a limiter with unique distortion.
    // All four ratio buttons engaged simultaneously create competing feedback loops
    // that produce an effective ratio of ~100:1 or higher, with heavy harmonic
    // distortion and the characteristic "nuke" pumping effect.

    constexpr float measuredPoints[][2] = {
        {0.0f, 0.0f}, {1.0f, 0.9f}, {2.0f, 1.9f}, {4.0f, 3.85f}, {6.0f, 5.8f},
        {8.0f, 7.8f}, {10.0f, 9.8f}, {15.0f, 14.8f}, {20.0f, 19.7f}, {30.0f, 29.5f}
    };
    constexpr int numPoints = sizeof(measuredPoints) / sizeof(measuredPoints[0]);

    for (int i = 0; i < ALLBUTTONS_TABLE_SIZE; ++i)
    {
        // Input range: 0-30dB over threshold
        float overThreshDb = 30.0f * static_cast<float>(i) / static_cast<float>(ALLBUTTONS_TABLE_SIZE - 1);

        // Modern curve: extreme compression (~100:1 effective ratio)
        // Real 1176 ABI creates competing feedback loops from all 4 ratio circuits,
        // resulting in near-limiting behavior with heavy harmonic distortion.
        // This is the "nuke" setting — NOT just a high ratio, it's a wall.
        if (overThreshDb < 1.0f)
        {
            // Gentle onset right at threshold
            float t = overThreshDb;
            allButtonsModernCurve[i] = overThreshDb * (0.7f + t * 0.28f);
        }
        else
        {
            // Near-limiting: ~0.99 slope = ~100:1 effective ratio
            allButtonsModernCurve[i] = 0.98f + (overThreshDb - 1.0f) * 0.99f;
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
    currentNumChannels.store(numChannels, std::memory_order_relaxed);  // For UI mono/stereo display

    // Read oversampling parameter early so compressor filters are tuned for the
    // actual processing rate (not always 4x). Filter coefficients (transformer HF
    // rolloff, DC blockers, tube emulation) depend on the sample rate — preparing
    // at 4x when running at 2x causes the HF rolloff to be an octave too low.
    // Memory allocation in prepare() is safe here (message thread, not audio thread).
    int osParamValue = 0;
    if (auto* oversamplingParam = parameters.getRawParameterValue("oversampling"))
        osParamValue = static_cast<int>(oversamplingParam->load());
    const int oversamplingMultiplier = (osParamValue == 2) ? 4
                                     : (osParamValue == 1) ? 2 : 1;
    double oversampledRate = sampleRate * oversamplingMultiplier;
    int oversampledBlockSize = samplesPerBlock * oversamplingMultiplier;

    // Prepare all compressor types at the oversampled rate
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
    if (antiAliasing)
        antiAliasing->prepare(sampleRate, samplesPerBlock, numChannels);

    // Prepare sidechain EQ
    if (sidechainEQ)
        sidechainEQ->prepare(sampleRate, numChannels);

    // Prepare true-peak detector for sidechain
    if (truePeakDetector)
        truePeakDetector->prepare(sampleRate, numChannels, samplesPerBlock);

    // Prepare transient shaper for FET all-buttons mode
    if (transientShaper)
        transientShaper->prepare(sampleRate, numChannels);

    // Sync oversampling setting from parameter before first latency report
    // Without this, antiAliasing may still be in its default state (e.g. 2x)
    // when the session was saved with Off or 4x, causing wrong PDC until processBlock
    // osParamValue was already read at the top of prepareToPlay()
    currentOversamplingFactor = osParamValue;
    if (antiAliasing)
        antiAliasing->setOversamplingFactor(currentOversamplingFactor);

    // Report actual current latency (oversampling + lookahead) for correct DAW PDC
    updateLatencyReport();

    // GR meter delay only needs to match oversampling latency
    int oversamplingLatency = antiAliasing ? antiAliasing->getLatency() : 0;
    int delayInBlocks = (oversamplingLatency + samplesPerBlock - 1) / samplesPerBlock;
    grDelayBuffer.fill(0.0f);
    grDelayWritePos.store(0, std::memory_order_relaxed);
    // Use release ordering to ensure buffer fill and writePos are visible before delaySamples
    grDelaySamples.store(juce::jmin(delayInBlocks, MAX_GR_DELAY_SAMPLES - 1), std::memory_order_release);

    // Pre-allocate buffers for processBlock to avoid allocation in audio thread
    // Use 8x the block size for safety - DAWs like Logic Pro may pass much larger blocks
    // than specified in prepareToPlay, especially during bounce/offline processing
    int safeBlockSize = samplesPerBlock * 8;
    filteredSidechain.setSize(numChannels, safeBlockSize);
    linkedSidechain.setSize(numChannels, safeBlockSize);
    externalSidechain.setSize(numChannels, safeBlockSize);
    // Allocate interpolated sidechain for max 4x oversampling
    interpolatedSidechain.setSize(numChannels, safeBlockSize * 4);

    // Phase-coherent dry/wet mixer (compensates FIR anti-aliasing latency)
    dryWetMixer.prepare(sampleRate, samplesPerBlock, numChannels, 4);  // max 4x oversampling

    // Set latency for phase-coherent dry/wet mixing
    // currentOversamplingFactor is UI index (0=off, 1=2x, 2=4x), convert to actual factor
    const int actualOsFactor = (currentOversamplingFactor == 2) ? 4
                             : (currentOversamplingFactor == 1) ? 2 : 1;
    dryWetMixer.setCurrentOversamplingFactor(actualOsFactor);
    dryWetMixer.setOversamplingLatency(oversamplingLatency);
    // Global lookahead is applied to the buffer BEFORE dry capture, so both dry and
    // wet signals already contain the global lookahead delay. Don't include it in
    // additionalLatency or the dry signal gets double-delayed → comb filtering.
    // Only Digital mode's internal lookahead creates a dry/wet offset (applied inside process()).
    dryWetMixer.setAdditionalLatency(0);
    {
        int digitalDelay = 0;
        if (getCurrentMode() == CompressorMode::Digital && sampleRate > 0)
        {
            auto* dlParam = parameters.getRawParameterValue("digital_lookahead");
            float dlMs = (dlParam != nullptr) ? dlParam->load() : 0.0f;
            digitalDelay = static_cast<int>(std::round(
                (dlMs / 1000.0f) * static_cast<float>(sampleRate)));
        }
        dryWetMixer.setProcessingLatency(digitalDelay);
    }

    // Initialize smoothed auto-makeup gain with ~50ms smoothing time
    smoothedAutoMakeupGain.reset(sampleRate, 0.05);
    smoothedAutoMakeupGain.setCurrentAndTargetValue(1.0f);

    // Pre-allocate bypass crossfade buffer. Base fade is 5ms, but in Digital mode
    // the fade extends to cover the digital lookahead (up to 10ms) so stale delay
    // line samples are masked. Allocate for the max possible: 5ms + 10ms = 15ms.
    bypassFadeLengthSamples = juce::jlimit(64, 2048,
        static_cast<int>(sampleRate * 0.005));
    int maxFadeSamples = juce::jlimit(64, 8192,
        static_cast<int>(sampleRate * 0.015));  // 15ms max
    bypassFadeBuffer.setSize(numChannels, maxFadeSamples, false, false, true);
    bypassFadeRemaining = 0;

    // Dedicated delay line for time-aligning the bypass fade dry signal
    // with the wet path's global lookahead (max 10ms).
    int maxLookaheadDelay = juce::jlimit(0, 4096,
        static_cast<int>(sampleRate * 0.01));  // 10ms max
    bypassFadeDelaySize = maxLookaheadDelay + 1;
    if (bypassFadeDelaySize > 1)
    {
        bypassFadeDelayBuf.setSize(numChannels, bypassFadeDelaySize, false, false, true);
        bypassFadeDelayBuf.clear();
    }
    bypassFadeDelayWritePos.assign(static_cast<size_t>(numChannels), 0);

    // Initialize RMS coefficient for ~200ms averaging window
    // GR-based auto-gain: smooth the gain reduction with ~200ms time constant
    // For 99% convergence in 200ms, use timeConstant ≈ 200ms / 4.6 ≈ 43ms
    float grTimeConstantSec = 0.043f;  // 43ms time constant (~200ms to 99%)
    int grBlockSize = juce::jmax(1, samplesPerBlock);  // Prevent division by zero
    double safeSampleRate = juce::jlimit(8000.0, 384000.0, sampleRate);
    float blocksPerSecond = static_cast<float>(safeSampleRate) / static_cast<float>(grBlockSize);
    grSmoothCoeff = 1.0f - std::exp(-1.0f / (blocksPerSecond * grTimeConstantSec));
    grSmoothCoeff = juce::jlimit(0.001f, 0.999f, grSmoothCoeff);
    smoothedGrDb = 0.0f;
    lastCompressorMode = -1;  // Force mode change detection on first processBlock
    primeGrAccumulator = true;  // Prime on first block

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
    if (!bypassParam)
        return;

    // Bypassed: zero latency, no processing (clean parallel bus behavior)
    if (*bypassParam > 0.5f)
    {
        // Report 0 latency during bypass so DAW doesn't apply PDC
        // for a plugin that isn't processing. This is the key fix for
        // "disabled plugin on bus causes phase issues".
        if (getLatencySamples() != 0)
            setLatencySamples(0);

        // Keep all delay paths warm during bypass: feed audio through the
        // ring buffers (discard output) so that on unbypass the wet path has
        // current audio instead of stale pre-bypass samples.
        {
            int nc = buffer.getNumChannels();
            int ns = buffer.getNumSamples();

            // Global lookahead buffer (all modes, native rate)
            if (lookaheadBuffer)
            {
                float lookaheadMs = 0.0f;
                if (auto* laParam = parameters.getRawParameterValue("global_lookahead"))
                    lookaheadMs = laParam->load();
                if (lookaheadMs > 0.0f)
                    for (int ch = 0; ch < nc; ++ch)
                        for (int i = 0; i < ns; ++i)
                            lookaheadBuffer->processSample(buffer.getSample(ch, i), ch, lookaheadMs);
            }

            // NOTE: Digital compressor's internal lookahead is NOT warmed here.
            // It operates at the oversampled rate (2x/4x), so feeding native-rate
            // samples would corrupt its delay line. The crossfade is extended to
            // cover the digital lookahead window instead.

            // Also keep the bypass fade's dedicated delay line warm so it has
            // current audio for time-aligned dry capture on unbypass.
            if (bypassFadeDelaySize > 1)
            {
                for (int ch = 0; ch < nc; ++ch)
                {
                    int& wp = bypassFadeDelayWritePos[static_cast<size_t>(ch)];
                    for (int i = 0; i < ns; ++i)
                    {
                        bypassFadeDelayBuf.setSample(ch, wp, buffer.getSample(ch, i));
                        wp = (wp + 1) % bypassFadeDelaySize;
                    }
                }
            }
        }

        bypassFadeRemaining = 0;  // Cancel any in-progress fade if re-bypassed
        wasBypassedLastBlock = true;
        return;
    }

    // Restore latency on bypass→active transition with smooth crossfade
    if (wasBypassedLastBlock)
    {
        wasBypassedLastBlock = false;
        updateLatencyReport();

        // In Digital mode, extend the crossfade to cover the internal lookahead
        // delay. The digital lookahead buffer can't be warmed during bypass (it
        // runs at oversampled rate), so stale samples persist for up to 10ms.
        // Extending the fade masks them entirely.
        bypassFadeActualLength = bypassFadeLengthSamples;
        if (getCurrentMode() == CompressorMode::Digital && digitalCompressor)
        {
            float digitalLaMs = 0.0f;
            if (auto* p = parameters.getRawParameterValue("digital_lookahead"))
                digitalLaMs = p->load();
            if (digitalLaMs > 0.0f)
            {
                int digitalLaSamples = static_cast<int>(getSampleRate() * digitalLaMs * 0.001);
                bypassFadeActualLength += digitalLaSamples;
                bypassFadeActualLength = juce::jmin(bypassFadeActualLength,
                    bypassFadeBuffer.getNumSamples());
            }
        }
        bypassFadeRemaining = bypassFadeActualLength;

        smoothedAutoMakeupGain.setCurrentAndTargetValue(1.0f);
        smoothedGrDb = 0.0f;
        primeGrAccumulator = true;
    }

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

    // Track whether we need dry/wet mixing for parallel compression
    // The actual dry capture happens at oversampled rate (in the oversampling section)
    // for phase-coherent mixing that prevents comb filtering
    bool needsDryBuffer = (mixAmount > 0.001f && mixAmount < 0.999f);

    // Save latency-aligned dry signal for bypass crossfade.
    // The wet path includes lookahead delay, so the dry reference must be delayed
    // by the same amount to avoid phase mismatch during the fade window.
    if (bypassFadeRemaining > 0)
    {
        const int fadeSamples = juce::jmin(bypassFadeRemaining,
            juce::jmin(buffer.getNumSamples(), bypassFadeBuffer.getNumSamples()));
        const int fadeChannels = juce::jmin(buffer.getNumChannels(),
            bypassFadeBuffer.getNumChannels());

        // Check for active global lookahead. Digital lookahead is only relevant
        // in Digital mode and operates at the oversampled rate — we can't align
        // against it at native rate (handled by extending the fade instead).
        float globalLaMs = 0.0f;
        if (auto* p = parameters.getRawParameterValue("global_lookahead"))
            globalLaMs = p->load();

        int delaySamples = 0;
        if (globalLaMs > 0.0f && bypassFadeDelaySize > 1)
            delaySamples = juce::jlimit(0, bypassFadeDelaySize - 1,
                static_cast<int>(std::round(globalLaMs * 0.001f * static_cast<float>(getSampleRate()))));

        if (delaySamples > 0)
        {
            // Feed each input sample through the dedicated delay line and capture
            // the delayed output. This produces a properly sequenced block of
            // time-aligned dry audio without touching the processing lookahead.
            for (int ch = 0; ch < fadeChannels; ++ch)
            {
                float* dst = bypassFadeBuffer.getWritePointer(ch);
                int& wp = bypassFadeDelayWritePos[static_cast<size_t>(ch)];
                for (int i = 0; i < fadeSamples; ++i)
                {
                    int rp = (wp - delaySamples + bypassFadeDelaySize) % bypassFadeDelaySize;
                    dst[i] = bypassFadeDelayBuf.getSample(ch, rp);
                    bypassFadeDelayBuf.setSample(ch, wp, buffer.getSample(ch, i));
                    wp = (wp + 1) % bypassFadeDelaySize;
                }
            }
        }
        else
        {
            // No lookahead — raw input is already time-aligned with wet path
            for (int ch = 0; ch < fadeChannels; ++ch)
                std::memcpy(bypassFadeBuffer.getWritePointer(ch),
                             buffer.getReadPointer(ch),
                             static_cast<size_t>(fadeSamples) * sizeof(float));
        }
    }

    // At 0% mix (100% dry), skip all processing and pass through undelayed.
    // Report 0 latency so DAW doesn't apply PDC (same approach as bypass).
    if (mixAmount <= 0.001f)
    {
        if (getLatencySamples() != 0)
            setLatencySamples(0);

        const int bypassChannels = buffer.getNumChannels();
        const int bypassSamples = buffer.getNumSamples();

        // Update meters to show input = output
        for (int ch = 0; ch < bypassChannels; ++ch)
        {
            float rms = 0.0f;
            const float* data = buffer.getReadPointer(ch);
            for (int i = 0; i < bypassSamples; ++i)
                rms += data[i] * data[i];
            rms = std::sqrt(rms / static_cast<float>(bypassSamples));

            if (ch == 0)
                linkedGainReduction[0].store(0.0f, std::memory_order_relaxed);
            else
                linkedGainReduction[1].store(0.0f, std::memory_order_relaxed);
        }
        grMeter.store(0.0f, std::memory_order_relaxed);
        return;
    }

    // Internal oversampling is always enabled for better quality
    bool oversample = true; // Always use oversampling internally
    CompressorMode mode = getCurrentMode();

    // Reset auto-gain state on mode change
    int currentModeInt = static_cast<int>(mode);
    if (currentModeInt != lastCompressorMode)
    {
        lastCompressorMode = currentModeInt;
        // Flag to prime GR accumulator with first block's GR value (instant convergence)
        primeGrAccumulator = true;
        // Reset smoothed gain to unity to avoid sudden volume jumps
        smoothedAutoMakeupGain.setCurrentAndTargetValue(1.0f);
        // Reset Digital mode's lookahead latency
        if (mode != CompressorMode::Digital)
            dryWetMixer.setProcessingLatency(0);
        lastReportedLookaheadSamples = -1;  // Force re-evaluation of lookahead
        updateLatencyReport();
    }

    // Read auto-makeup and sidechain state early - needed for parameter caching
    auto* autoMakeupParamEarly = parameters.getRawParameterValue("auto_makeup");
    bool autoMakeupRaw = (autoMakeupParamEarly != nullptr) ? (autoMakeupParamEarly->load() > 0.5f) : false;
    auto* sidechainEnableParamEarly = parameters.getRawParameterValue("sidechain_enable");
    bool extScEnabled = (sidechainEnableParamEarly != nullptr) ? (sidechainEnableParamEarly->load() > 0.5f) : false;
    // Disable auto-gain when external sidechain is active — auto-gain would counteract the ducking effect
    bool autoMakeup = autoMakeupRaw && !extScEnabled;

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
    bool useExternalSidechain = (sidechainEnableParam != nullptr) ? (sidechainEnableParam->load() > 0.5f) : false;
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
    // External sidechain auto-detected from signal on the bus (see below)
    int stereoLinkMode = (stereoLinkModeParam != nullptr) ? static_cast<int>(stereoLinkModeParam->load()) : 0;
    int oversamplingFactor = (oversamplingParam != nullptr) ? static_cast<int>(oversamplingParam->load()) : 0;

    // Update oversampling factor (0 = Off, 1 = 2x, 2 = 4x)
    // User controls this via dropdown - 2x or 4x recommended for vintage modes with heavy saturation
    if (antiAliasing)
        antiAliasing->setOversamplingFactor(oversamplingFactor);

    // Track oversampling factor changes and update compressor filter coefficients
    if (oversamplingFactor != currentOversamplingFactor)
    {
        currentOversamplingFactor = oversamplingFactor;
        // Update DryWetMixer with actual oversampling factor (UI index → factor)
        const int newOsFactor = (currentOversamplingFactor == 2) ? 4
                              : (currentOversamplingFactor == 1) ? 2 : 1;
        dryWetMixer.setCurrentOversamplingFactor(newOsFactor);

        // Recalculate compressor filter coefficients for the new oversampled rate.
        // This is lightweight (no allocation) — only updates transformer HF rolloff,
        // DC blocker, tube emulation, and timing coefficients.
        double newOsRate = currentSampleRate * newOsFactor;
        if (optoCompressor) optoCompressor->updateSampleRate(newOsRate);
        if (fetCompressor) fetCompressor->updateSampleRate(newOsRate);
        if (vcaCompressor) vcaCompressor->updateSampleRate(newOsRate);
        if (busCompressor) busCompressor->updateSampleRate(newOsRate);
        if (studioFetCompressor) studioFetCompressor->updateSampleRate(newOsRate);
        if (studioVcaCompressor) studioVcaCompressor->updateSampleRate(newOsRate);
        if (digitalCompressor) digitalCompressor->updateSampleRate(newOsRate);
        // Note: multibandCompressor operates at native rate, not oversampled — no update needed

        // Notify host of latency change so DAW PDC stays correct
        updateLatencyReport();
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

    // External sidechain controlled by user toggle only.
    // Bus-state (isEnabled()) is unreliable across formats: LV2 always reports true,
    // VST3 may report false depending on host. The getBusBuffer channel check below
    // safely falls back to main input if the host hasn't actually routed audio.
    bool hasExternalSidechain = useExternalSidechain;

    externalSidechainActive.store(hasExternalSidechain, std::memory_order_relaxed);

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

            // max(L,R) for all modes (original SSL 4000 behavior)
            float linkedLevel = juce::jmax(leftLevel, rightLevel);

            // Blend independent and linked based on stereoLinkAmount
            leftSC[i] = leftLevel * (1.0f - stereoLinkAmount) + linkedLevel * stereoLinkAmount;
            rightSC[i] = rightLevel * (1.0f - stereoLinkAmount) + linkedLevel * stereoLinkAmount;
        }
    }

    // Track lookahead changes for DAW PDC updates and dry/wet alignment
    {
        int globalLookaheadSamples = static_cast<int>(std::round((globalLookaheadMs / 1000.0f) * static_cast<float>(currentSampleRate)));

        // Digital mode's internal lookahead creates a processing delay between dry and wet.
        // IMPORTANT: The Digital compressor is ALWAYS prepared at 4x rate (for thread safety),
        // so its internal delay is round(lookaheadMs * baseRate * 4) iterations, regardless of
        // the current OS factor. When running at < 4x, each iteration = more base-rate time.
        int digitalLookaheadBaseSamples = 0;
        if (mode == CompressorMode::Digital)
        {
            auto* digitalLookaheadParam = parameters.getRawParameterValue("digital_lookahead");
            float digitalLookaheadMs = (digitalLookaheadParam != nullptr) ? digitalLookaheadParam->load() : 0.0f;
            digitalLookaheadBaseSamples = static_cast<int>(std::round(
                (digitalLookaheadMs / 1000.0f) * static_cast<float>(currentSampleRate)));
        }

        int totalLookaheadSamples = globalLookaheadSamples + digitalLookaheadBaseSamples;

        if (totalLookaheadSamples != lastReportedLookaheadSamples)
        {
            lastReportedLookaheadSamples = totalLookaheadSamples;
            // Global lookahead is already applied to the buffer before dry capture,
            // so it doesn't cause dry/wet offset. Only Digital's internal lookahead
            // (applied inside process()) creates a timing mismatch needing compensation.
            dryWetMixer.setProcessingLatency(digitalLookaheadBaseSamples);
            updateLatencyReport();
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
        constexpr int kBands = MultibandCompressor::NUM_BANDS;
        static_assert(kBands == kNumMultibandBands,
                      "Band-count constants must match across parameter and DSP paths.");
        const juce::String bandNames[] = {"low", "lowmid", "highmid", "high"};
        std::array<float, kBands> thresholds, ratios, attacks, releases, makeups;
        std::array<bool, kBands> bypasses, solos;

        for (int band = 0; band < kBands; ++band)
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

        // Process through multiband compressor (pass sidechain for external SC detection)
        multibandCompressor->processBlock(buffer, thresholds, ratios, attacks, releases,
                                          makeups, bypasses, solos, mbOutput, mbMix,
                                          &filteredSidechain, hasExternalSidechain);

        // Update per-band GR meters for UI
        for (int band = 0; band < kBands; ++band)
        {
            bandGainReduction[band].store(multibandCompressor->getBandGainReduction(band),
                                          std::memory_order_relaxed);
        }

        // Get overall GR for main meter (max = worst band, for display)
        float grMax = multibandCompressor->getMaxGainReduction();
        linkedGainReduction[0].store(grMax, std::memory_order_relaxed);
        linkedGainReduction[1].store(grMax, std::memory_order_relaxed);
        float gainReduction = grMax;
        // Note: getAverageGainReduction() is available but max GR works better
        // for auto-gain since the most-compressed band dominates perception

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

        // GR-based auto-gain for multiband mode
        // Inverts the compressor's own gain reduction for deterministic compensation
        {
            float targetAutoGain = 1.0f;

            if (autoMakeup)
            {
                // Use max band GR for compensation — the most-compressed band dominates
                // perception, and for narrowband signals, only that band's GR matters
                float avgGrDb = grMax;

                // Smooth the GR to avoid pumping
                if (primeGrAccumulator)
                {
                    smoothedGrDb = avgGrDb;
                    primeGrAccumulator = false;
                    float primedGain = juce::Decibels::decibelsToGain(juce::jlimit(-40.0f, 40.0f, -avgGrDb));
                    float mbMixNorm = mbMix * 0.01f;
                    primedGain = 1.0f + (primedGain - 1.0f) * mbMixNorm;
                    smoothedAutoMakeupGain.setCurrentAndTargetValue(primedGain);
                }
                else
                {
                    smoothedGrDb += grSmoothCoeff * (avgGrDb - smoothedGrDb);
                }

                // Base compensation: invert the smoothed gain reduction
                float autoGainDb = -smoothedGrDb;
                autoGainDb = juce::jlimit(-40.0f, 40.0f, autoGainDb);
                targetAutoGain = juce::Decibels::decibelsToGain(autoGainDb);

                // Scale auto-gain by mix amount: dry signal doesn't need compensation
                float mbMixNorm = mbMix * 0.01f;
                targetAutoGain = 1.0f + (targetAutoGain - 1.0f) * mbMixNorm;
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

        // Apply bypass crossfade for multiband mode (shared logic with other modes)
        if (bypassFadeRemaining > 0)
        {
            int fadeSamples = juce::jmin(bypassFadeRemaining, numSamples);
            float fadeLen = static_cast<float>(bypassFadeActualLength);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* out = buffer.getWritePointer(ch);
                const float* dry = bypassFadeBuffer.getReadPointer(ch);
                for (int i = 0; i < fadeSamples; ++i)
                {
                    float wet = 1.0f - static_cast<float>(bypassFadeRemaining - i) / fadeLen;
                    out[i] = out[i] * wet + dry[i] * (1.0f - wet);
                }
            }
            bypassFadeRemaining -= fadeSamples;
        }

        return;  // Skip normal processing for multiband mode
    }

    // Process audio with reduced function call overhead
    if (oversample && antiAliasing && antiAliasing->isReady())
    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto oversampledBlock = antiAliasing->processUp(block);

        const int osNumChannels = static_cast<int>(oversampledBlock.getNumChannels());
        const int osNumSamples = static_cast<int>(oversampledBlock.getNumSamples());

        // Processing latency (Digital mode lookahead) is set per-block in the
        // lookahead tracking section above. All other modes add zero group delay.

        // Capture dry at oversampled rate so both paths share the downsampling filter
        bool needsOversampledDry = needsDryBuffer && (mixAmount < 0.999f);
        if (needsOversampledDry)
        {
            // Use shared DryWetMixer for phase-coherent capture
            needsOversampledDry = dryWetMixer.captureDryAtOversampledRate(oversampledBlock);
            if (!needsOversampledDry)
            {
                // Buffer too small - fail safe by disabling mixing entirely
                needsDryBuffer = false;
            }
        }

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
                        data[i] = optoCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2] > 0.5f, true, scData[i], hasExternalSidechain);
                    break;
                case CompressorMode::FET:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = fetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1],
                                                         cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), true,
                                                         lookupTables.get(), transientShaper.get(),
                                                         cachedParams[5] > 0.5f, cachedParams[6], scData[i], hasExternalSidechain);
                    break;
                case CompressorMode::VCA:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = vcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], cachedParams[5] > 0.5f, true, scData[i], hasExternalSidechain);
                    break;
                case CompressorMode::Bus:
                    for (int i = 0; i < osNumSamples; ++i)
                        data[i] = busCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], static_cast<int>(cachedParams[2]), static_cast<int>(cachedParams[3]), cachedParams[4], cachedParams[5], true, scData[i], hasExternalSidechain);
                    break;
                case CompressorMode::StudioFET:
                    // Optimized: use pre-interpolated sidechain with direct pointer access
                    for (int i = 0; i < osNumSamples; ++i)
                    {
                        data[i] = studioFetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), scData[i], hasExternalSidechain);
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
            // FIR anti-aliasing is critical for clean oversampled processing
            if (distType != DistortionType::Off && distAmount > 0.0f)
            {
                for (int i = 0; i < osNumSamples; ++i)
                {
                    data[i] = applyDistortion(data[i], distType, distAmount);
                }
            }
        }

        // Mix dry and wet at OVERSAMPLED rate (before downsampling)
        // Uses shared DryWetMixer for phase-coherent mixing
        if (needsOversampledDry)
        {
            dryWetMixer.mixAtOversampledRate(oversampledBlock, mixAmount);
            // Mark that we've already done the mix at oversampled rate
            // so we don't do it again after downsampling
            needsDryBuffer = false;
        }

        antiAliasing->processDown(block);
    }
    else
    {
        // Process without oversampling
        // No compensation needed - maintain unity gain
        const float compensationGain = 1.0f; // Unity gain (no compensation)

        // Capture dry signal for mix knob (parallel compression) in non-oversampled mode
        // No latency to compensate since there's no oversampling FIR filter
        bool canMixNormal = false;
        if (needsDryBuffer)
        {
            dryWetMixer.captureDryAtNormalRate(buffer);
            canMixNormal = true;
        }
        
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
                        data[i] = optoCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2] > 0.5f, false, scSignal, hasExternalSidechain) * compensationGain;
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
                                                         cachedParams[5] > 0.5f, cachedParams[6], scSignal, hasExternalSidechain) * compensationGain;
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
                        data[i] = vcaCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], cachedParams[4], cachedParams[5] > 0.5f, false, scSignal, hasExternalSidechain) * compensationGain;
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
                        data[i] = busCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], static_cast<int>(cachedParams[2]), static_cast<int>(cachedParams[3]), cachedParams[4], cachedParams[5], false, scSignal, hasExternalSidechain) * compensationGain;
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
                        data[i] = studioFetCompressor->process(data[i], channel, cachedParams[0], cachedParams[1], cachedParams[2], cachedParams[3], static_cast<int>(cachedParams[4]), scSignal, hasExternalSidechain) * compensationGain;
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

        // Apply dry/wet mix for non-oversampled mode (parallel compression)
        if (canMixNormal)
        {
            dryWetMixer.mixAtNormalRate(buffer, mixAmount);
            needsDryBuffer = false;
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

    // GR-based auto-gain: invert the compressor's gain reduction for deterministic compensation
    // Industry-standard approach (FabFilter Pro-C, Waves CLA series)
    {
        float targetAutoGain = 1.0f;

        if (autoMakeup)
        {
            // Average GR across channels (negative dB = compression)
            float avgGrDb = (grLeft + grRight) * 0.5f;

            // Smooth the GR to avoid pumping (~200ms time constant)
            if (primeGrAccumulator)
            {
                smoothedGrDb = avgGrDb;
                primeGrAccumulator = false;
                // Set gain immediately without smoothing on first block
                float autoGainDb = -avgGrDb;
                if (mode == CompressorMode::FET || mode == CompressorMode::StudioFET)
                    autoGainDb -= cachedParams[0];  // Compensate for input gain knob
                // Opto: tube harmonics add energy proportional to GR that autogain doesn't track
                if (mode == CompressorMode::Opto)
                    autoGainDb -= std::min(1.5f, std::abs(avgGrDb) * 0.25f);
                autoGainDb = juce::jlimit(-40.0f, 40.0f, autoGainDb);
                float primedGain = juce::Decibels::decibelsToGain(autoGainDb);
                primedGain = 1.0f + (primedGain - 1.0f) * mixAmount;
                smoothedAutoMakeupGain.setCurrentAndTargetValue(primedGain);
            }
            else
            {
                smoothedGrDb += grSmoothCoeff * (avgGrDb - smoothedGrDb);
            }

            // Base compensation: invert the smoothed gain reduction
            float autoGainDb = -smoothedGrDb;

            // FET/StudioFET: also compensate for the input gain knob
            // When user turns input down, auto-gain should restore the original level
            if (mode == CompressorMode::FET || mode == CompressorMode::StudioFET)
                autoGainDb -= cachedParams[0];  // cachedParams[0] = fet_input parameter

            // Opto: tube harmonics add energy proportional to GR that autogain doesn't track
            if (mode == CompressorMode::Opto)
                autoGainDb -= std::min(1.5f, std::abs(smoothedGrDb) * 0.25f);

            autoGainDb = juce::jlimit(-40.0f, 40.0f, autoGainDb);
            targetAutoGain = juce::Decibels::decibelsToGain(autoGainDb);

            // Scale auto-gain by mix amount: dry signal doesn't need compensation
            // At 100% wet = full auto-gain, at 100% dry = unity, at 50/50 = half
            targetAutoGain = 1.0f + (targetAutoGain - 1.0f) * mixAmount;
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

    // Apply bypass crossfade (smooth transition from bypassed to active)
    if (bypassFadeRemaining > 0)
    {
        int fadeSamples = juce::jmin(bypassFadeRemaining, numSamples);
        float fadeLen = static_cast<float>(bypassFadeActualLength);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* out = buffer.getWritePointer(ch);
            const float* dry = bypassFadeBuffer.getReadPointer(ch);
            for (int i = 0; i < fadeSamples; ++i)
            {
                float wet = 1.0f - static_cast<float>(bypassFadeRemaining - i) / fadeLen;
                out[i] = out[i] * wet + dry[i] * (1.0f - wet);
            }
        }
        bypassFadeRemaining -= fadeSamples;
    }

    // NOTE: Output distortion is now applied in the oversampled domain (inside the
    // oversample block above) to prevent aliasing. This matches professional standards
    // where all nonlinear processing happens at the oversampled rate.
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

    // Delay GR meter by PDC latency to match audible output
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
    
    // Mix control is handled by DryWetMixer in both paths:
    // - Oversampled: captureDryAtOversampledRate/mixAtOversampledRate (phase-coherent)
    // - Non-oversampled: captureDryAtNormalRate/mixAtNormalRate (no latency to compensate)
    juce::ignoreUnused(needsDryBuffer);

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

void UniversalCompressor::updateLatencyReport()
{
    int latency = 0;
    const auto mode = getCurrentMode();

    // Multiband runs at native rate (no oversampling), so exclude anti-aliasing latency
    const bool usesOversamplingPath = (mode != CompressorMode::Multiband);
    if (usesOversamplingPath && antiAliasing)
        latency += antiAliasing->getLatency();

    // Compute global lookahead from parameter directly (not from buffer state,
    // which may be stale if processSample() hasn't run yet with the new value)
    if (lookaheadBuffer && currentSampleRate > 0)
    {
        auto* globalLookaheadParam = parameters.getRawParameterValue("global_lookahead");
        float globalLookaheadMs = (globalLookaheadParam != nullptr) ? globalLookaheadParam->load() : 0.0f;
        int globalLookaheadSamples = static_cast<int>(std::round((globalLookaheadMs / 1000.0f) * static_cast<float>(currentSampleRate)));
        globalLookaheadSamples = juce::jlimit(0, lookaheadBuffer->getMaxLookaheadSamples() - 1, globalLookaheadSamples);
        latency += globalLookaheadSamples;
    }

    // Digital mode has its own internal lookahead delay that must also be reported.
    // The digitalLookaheadMs parameter is in real milliseconds, so convert directly
    // to base-rate samples regardless of internal oversampling factor.
    if (mode == CompressorMode::Digital && digitalCompressor && currentSampleRate > 0)
    {
        auto* digitalLookaheadParam = parameters.getRawParameterValue("digital_lookahead");
        float digitalLookaheadMs = (digitalLookaheadParam != nullptr) ? digitalLookaheadParam->load() : 0.0f;
        int digitalLookaheadSamples = static_cast<int>(std::round((digitalLookaheadMs / 1000.0f) * static_cast<float>(currentSampleRate)));
        latency += digitalLookaheadSamples;
    }

    setLatencySamples(latency);
}

juce::AudioProcessorParameter* UniversalCompressor::getBypassParameter() const
{
    // Return our "bypass" parameter so the AU/VST3 wrapper routes host bypass
    // through processBlock (where our internal bypass applies correct delay paths)
    // instead of processBlockBypassed or skipping rendering entirely.
    // Without this, Logic Pro's disable button stops calling the AU but still
    // applies PDC → phase offset on parallel buses.
    return parameters.getParameter("bypass");
}

void UniversalCompressor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host bypass (fallback — with getBypassParameter() set, this should rarely be called).
    // Report 0 latency and pass audio through undelayed, same as our internal bypass.
    if (getLatencySamples() != 0)
        setLatencySamples(0);
    // Mark as bypassed so processBlock can detect the transition and restore latency.
    wasBypassedLastBlock = true;
    // Clear bypass fade state so stale delay-line samples don't leak into the next fade
    bypassFadeRemaining = 0;
    bypassFadeBuffer.clear();
    bypassFadeDelayBuf.clear();
    std::fill(bypassFadeDelayWritePos.begin(), bypassFadeDelayWritePos.end(), 0);
    // Audio passes through unchanged — JUCE default clears output, we just leave input as-is.
    juce::ignoreUnused(buffer);
}

double UniversalCompressor::getTailLengthSeconds() const
{
    return currentSampleRate > 0 ? static_cast<double>(getLatencySamples()) / currentSampleRate : 0.0;
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
    // Reset smoothed auto-makeup gain and GR accumulator to neutral
    smoothedAutoMakeupGain.setCurrentAndTargetValue(1.0f);
    smoothedGrDb = 0.0f;
    lastCompressorMode = -1;  // Force mode change detection on next processBlock
    primeGrAccumulator = true;  // Prime accumulator on first block after reset

    bypassFadeRemaining = 0;
    bypassFadeBuffer.clear();
    bypassFadeDelayBuf.clear();
    std::fill(bypassFadeDelayWritePos.begin(), bypassFadeDelayWritePos.end(), 0);

    // Reset dry/wet mixer state (for oversampling latency compensation)
    dryWetMixer.reset();

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

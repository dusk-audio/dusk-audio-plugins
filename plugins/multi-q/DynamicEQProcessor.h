#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

//==============================================================================
/**
 * DynamicEQProcessor - Per-band envelope followers for FabFilter Pro-Q style
 * dynamic EQ processing.
 *
 * Each band can independently have dynamics enabled, with its own:
 * - Threshold (dB)
 * - Attack time (ms)
 * - Release time (ms)
 * - Range (dB) - maximum gain change
 *
 * When input level at the band's frequency exceeds threshold, the band's
 * gain is dynamically reduced toward 0 dB. This is like a per-band compressor.
 */
class DynamicEQProcessor
{
public:
    static constexpr int NUM_BANDS = 8;

    //==========================================================================
    /** Per-band dynamic parameters */
    struct BandParameters
    {
        float threshold = 0.0f;      // dB (-60 to +12)
        float attack = 10.0f;        // ms (0.1 to 500)
        float release = 100.0f;      // ms (10 to 5000)
        float range = 12.0f;         // dB (0 to 24) - max gain change
        bool enabled = false;        // Per-band dynamics on/off
    };

    //==========================================================================
    DynamicEQProcessor() = default;

    /** Prepare the processor for playback */
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;

        // Initialize channel states
        channelStates.resize(static_cast<size_t>(numChannels));
        for (auto& ch : channelStates)
        {
            for (auto& band : ch.bands)
            {
                band.envelope = 0.0f;
                band.currentGainDb = 0.0f;
                band.smoothedGainDb = 0.0f;
            }
        }

        // Initialize detection bandpass filters (one per band per channel)
        detectionFilters.resize(static_cast<size_t>(numChannels));
        for (auto& ch : detectionFilters)
        {
            ch.resize(NUM_BANDS);
            for (auto& filter : ch)
                filter.reset();
        }

        // Reset gain meters
        for (auto& meter : dynamicGainMeters)
            meter.store(0.0f, std::memory_order_relaxed);
    }

    /** Reset all envelope followers and filters */
    void reset()
    {
        for (auto& ch : channelStates)
        {
            for (auto& band : ch.bands)
            {
                band.envelope = 0.0f;
                band.currentGainDb = 0.0f;
                band.smoothedGainDb = 0.0f;
            }
        }

        for (auto& ch : detectionFilters)
        {
            for (auto& filter : ch)
                filter.reset();
        }

        for (auto& meter : dynamicGainMeters)
            meter.store(0.0f, std::memory_order_relaxed);
    }

    //==========================================================================
    /** Set parameters for a specific band */
    void setBandParameters(int bandIndex, const BandParameters& params)
    {
        if (bandIndex >= 0 && bandIndex < NUM_BANDS)
            bandParams[static_cast<size_t>(bandIndex)] = params;
    }

    /** Get current parameters for a band */
    const BandParameters& getBandParameters(int bandIndex) const
    {
        return bandParams[static_cast<size_t>(juce::jlimit(0, NUM_BANDS - 1, bandIndex))];
    }

    /** Update detection filter for a band (call when band frequency changes) */
    void updateDetectionFilter(int bandIndex, float frequency, float q)
    {
        if (bandIndex < 0 || bandIndex >= NUM_BANDS)
            return;

        // Create bandpass filter coefficients for detection
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass(
            sampleRate, frequency, q);

        for (auto& ch : detectionFilters)
        {
            *ch[static_cast<size_t>(bandIndex)].coefficients = *coeffs;
        }
    }

    //==========================================================================
    /**
     * Process a single band and return the dynamic gain in dB.
     *
     * @param bandIndex  The band index (0-7)
     * @param inputLevel The input sample level (absolute value) for detection
     * @param channel    The channel index
     * @return Dynamic gain adjustment in dB (0 = no change, negative = reduction)
     */
    float processBand(int bandIndex, float inputLevel, int channel)
    {
        if (bandIndex < 0 || bandIndex >= NUM_BANDS ||
            channel < 0 || channel >= static_cast<int>(channelStates.size()))
            return 0.0f;

        const auto& params = bandParams[static_cast<size_t>(bandIndex)];

        // If dynamics not enabled for this band, return 0 (no gain change)
        if (!params.enabled)
            return 0.0f;

        auto& state = channelStates[static_cast<size_t>(channel)]
                          .bands[static_cast<size_t>(bandIndex)];

        // Convert input level to dB
        float inputDb = juce::Decibels::gainToDecibels(inputLevel, -96.0f);

        // Calculate attack/release coefficients
        float attackCoeff = calcCoefficient(params.attack);
        float releaseCoeff = calcCoefficient(params.release);

        // Envelope follower (one-pole filter)
        if (inputDb > state.envelope)
            state.envelope = attackCoeff * state.envelope + (1.0f - attackCoeff) * inputDb;
        else
            state.envelope = releaseCoeff * state.envelope + (1.0f - releaseCoeff) * inputDb;

        // Calculate dynamic gain
        state.currentGainDb = calculateDynamicGain(state.envelope, params);

        // Smooth the gain to avoid zipper noise (short smoothing)
        float smoothCoeff = calcCoefficient(2.0f); // 2ms smoothing
        state.smoothedGainDb = smoothCoeff * state.smoothedGainDb +
                               (1.0f - smoothCoeff) * state.currentGainDb;

        // Update meter (use max across channels for display)
        float currentMeter = dynamicGainMeters[static_cast<size_t>(bandIndex)]
                                 .load(std::memory_order_relaxed);
        if (std::abs(state.smoothedGainDb) > std::abs(currentMeter) || channel == 0)
        {
            dynamicGainMeters[static_cast<size_t>(bandIndex)]
                .store(state.smoothedGainDb, std::memory_order_relaxed);
        }

        return state.smoothedGainDb;
    }

    /**
     * Process detection for a band using its bandpass filter.
     * Returns the filtered level for sidechain detection.
     */
    float processDetection(int bandIndex, float input, int channel)
    {
        if (bandIndex < 0 || bandIndex >= NUM_BANDS ||
            channel < 0 || channel >= static_cast<int>(detectionFilters.size()))
            return std::abs(input);

        auto& filter = detectionFilters[static_cast<size_t>(channel)]
                           [static_cast<size_t>(bandIndex)];

        return std::abs(filter.processSample(input));
    }

    //==========================================================================
    /** Get current dynamic gain for UI visualization (thread-safe) */
    float getCurrentDynamicGain(int bandIndex) const
    {
        if (bandIndex >= 0 && bandIndex < NUM_BANDS)
            return dynamicGainMeters[static_cast<size_t>(bandIndex)]
                .load(std::memory_order_relaxed);
        return 0.0f;
    }

    /** Decay the gain meters (call from UI timer, ~30Hz) */
    void decayMeters(float decayAmount = 0.5f)
    {
        for (auto& meter : dynamicGainMeters)
        {
            float current = meter.load(std::memory_order_relaxed);
            if (std::abs(current) > 0.01f)
                meter.store(current * (1.0f - decayAmount), std::memory_order_relaxed);
            else
                meter.store(0.0f, std::memory_order_relaxed);
        }
    }

private:
    //==========================================================================
    /** Calculate one-pole filter coefficient from time in ms */
    float calcCoefficient(float timeMs) const
    {
        if (timeMs <= 0.0f)
            return 0.0f;
        // tau = time constant, coefficient = exp(-1 / (tau * sampleRate))
        float tau = timeMs / 1000.0f;
        return std::exp(-1.0f / (tau * static_cast<float>(sampleRate)));
    }

    /**
     * Calculate dynamic gain based on envelope vs threshold.
     * When envelope exceeds threshold, reduce the band's static gain toward 0.
     */
    float calculateDynamicGain(float envelopeDb, const BandParameters& params) const
    {
        // How much above threshold?
        float overThreshold = envelopeDb - params.threshold;

        if (overThreshold <= 0.0f)
        {
            // Below threshold - no dynamic gain change
            return 0.0f;
        }

        // Calculate gain reduction (soft knee)
        // The more over threshold, the more we reduce (up to range limit)
        // Using a simple ratio-like calculation
        float ratio = 4.0f; // Fixed ratio for now (could be a parameter)
        float reduction = overThreshold * (1.0f - 1.0f / ratio);

        // Limit to range parameter
        reduction = juce::jmin(reduction, params.range);

        // Return as negative dB (gain reduction)
        return -reduction;
    }

    //==========================================================================
    /** Per-channel, per-band envelope state */
    struct BandState
    {
        float envelope = 0.0f;        // Current envelope level (dB)
        float currentGainDb = 0.0f;   // Current dynamic gain (dB)
        float smoothedGainDb = 0.0f;  // Smoothed gain for output
    };

    struct ChannelState
    {
        std::array<BandState, NUM_BANDS> bands;
    };

    std::vector<ChannelState> channelStates;
    std::array<BandParameters, NUM_BANDS> bandParams;

    // Detection bandpass filters (per channel, per band)
    std::vector<std::vector<juce::dsp::IIR::Filter<float>>> detectionFilters;

    // Atomic meters for UI (one per band, shows max across channels)
    std::array<std::atomic<float>, NUM_BANDS> dynamicGainMeters;

    double sampleRate = 44100.0;
};

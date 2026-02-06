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
 * Enhanced features:
 * - Soft-knee compression for smoother response
 * - Lookahead for peak detection (prevents clipping on transients)
 *
 * When input level at the band's frequency exceeds threshold, the band's
 * gain is dynamically reduced toward 0 dB. This is like a per-band compressor.
 */
class DynamicEQProcessor
{
public:
    static constexpr int NUM_BANDS = 8;
    static constexpr int MAX_LOOKAHEAD_SAMPLES = 512;  // ~11ms at 44.1kHz

    //==========================================================================
    /** Per-band dynamic parameters */
    struct BandParameters
    {
        float threshold = -20.0f;    // dB (-48 to 0) - lower = more sensitive
        float attack = 10.0f;        // ms (0.1 to 500)
        float release = 100.0f;      // ms (10 to 5000)
        float range = 12.0f;         // dB (0 to 24) - max gain change
        float ratio = 4.0f;         // Compression ratio (1.0 to 100.0)
        float kneeWidth = 6.0f;      // dB (0 to 12) - soft knee width, 0 = hard knee
        bool enabled = false;        // Per-band dynamics on/off
    };

    //==========================================================================
    /** Global dynamic EQ settings */
    struct GlobalSettings
    {
        float lookaheadMs = 0.0f;    // 0 to 10ms lookahead
        bool softKneeEnabled = true;
    };

    //==========================================================================
    DynamicEQProcessor() = default;

    /** Prepare the processor for playback */
    void prepare(double newSampleRate, int channelCount)
    {
        sampleRate = newSampleRate;
        numChannels = channelCount;

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

        // Initialize lookahead buffers (per band, per channel)
        lookaheadBuffers.resize(static_cast<size_t>(numChannels));
        for (auto& ch : lookaheadBuffers)
        {
            ch.resize(NUM_BANDS);
            for (auto& band : ch)
            {
                band.buffer.resize(MAX_LOOKAHEAD_SAMPLES, 0.0f);
                band.writeIndex = 0;
                band.peakValue = 0.0f;
            }
        }

        // Reset gain meters
        for (auto& meter : dynamicGainMeters)
            meter.store(0.0f, std::memory_order_relaxed);

        // Recalculate lookahead samples for new sample rate
        int samples = static_cast<int>(globalSettings.lookaheadMs * sampleRate / 1000.0);
        samples = juce::jlimit(0, MAX_LOOKAHEAD_SAMPLES - 1, samples);
        lookaheadSamples.store(samples, std::memory_order_release);
    }

    /** Set global settings (thread-safe for audio thread access) */
    void setGlobalSettings(const GlobalSettings& settings)
    {
        globalSettings = settings;
        // Update atomics for thread-safe audio thread access
        softKneeEnabled.store(settings.softKneeEnabled, std::memory_order_release);
        // Calculate lookahead in samples
        int samples = static_cast<int>(settings.lookaheadMs * sampleRate / 1000.0);
        samples = juce::jlimit(0, MAX_LOOKAHEAD_SAMPLES - 1, samples);
        lookaheadSamples.store(samples, std::memory_order_release);
    }

    /** Get current lookahead in samples (for latency reporting) */
    int getLookaheadSamples() const { return lookaheadSamples.load(std::memory_order_acquire); }

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
        {
            BandParameters validatedParams = params;
            // Clamp ratio to valid range (1.0 to 100.0) to avoid division issues
            validatedParams.ratio = juce::jlimit(1.0f, 100.0f, params.ratio);
            bandParams[static_cast<size_t>(bandIndex)] = validatedParams;
        }
    }

    /** Get current parameters for a band */
    const BandParameters& getBandParameters(int bandIndex) const
    {
        return bandParams[static_cast<size_t>(juce::jlimit(0, NUM_BANDS - 1, bandIndex))];
    }

    /** Update detection filter for a band (call when band frequency changes)
     *  Thread-safe: Uses SpinLock to prevent race with processDetection() */
    void updateDetectionFilter(int bandIndex, float frequency, float q)
    {
        if (bandIndex < 0 || bandIndex >= NUM_BANDS)
            return;

        // Create bandpass filter coefficients for detection
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass(
            sampleRate, frequency, q);

        // Lock to prevent race condition with audio thread
        const juce::SpinLock::ScopedLockType lock(filterLock);
        for (auto& ch : detectionFilters)
        {
            *ch[static_cast<size_t>(bandIndex)].coefficients = *coeffs;
        }
    }

    //==========================================================================
    /**
     * Process a single band and return the dynamic gain in dB.
     * Uses lookahead if enabled for better peak detection on transients.
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

        // Use lookahead buffer if enabled (load atomic once for consistency)
        float detectionLevel = inputLevel;
        int currentLookahead = lookaheadSamples.load(std::memory_order_acquire);
        if (currentLookahead > 0 &&
            channel < static_cast<int>(lookaheadBuffers.size()) &&
            bandIndex < static_cast<int>(lookaheadBuffers[static_cast<size_t>(channel)].size()))
        {
            auto& lookahead = lookaheadBuffers[static_cast<size_t>(channel)][static_cast<size_t>(bandIndex)];

            // Store current sample in buffer
            lookahead.buffer[static_cast<size_t>(lookahead.writeIndex)] = inputLevel;

            // Find peak in lookahead window
            float peak = 0.0f;
            int bufSize = static_cast<int>(lookahead.buffer.size());
            for (int i = 0; i < currentLookahead; ++i)
            {
                int idx = (lookahead.writeIndex - i + bufSize) % bufSize;
                peak = juce::jmax(peak, lookahead.buffer[static_cast<size_t>(idx)]);
            }

            // Use peak for detection (allows gain reduction before the transient hits)
            detectionLevel = peak;

            // Advance write index
            lookahead.writeIndex = (lookahead.writeIndex + 1) % bufSize;
        }

        // Convert detection level to dB
        float inputDb = juce::Decibels::gainToDecibels(detectionLevel, -96.0f);

        // Calculate attack/release coefficients
        float attackCoeff = calcCoefficient(params.attack);
        float releaseCoeff = calcCoefficient(params.release);

        // Envelope follower (one-pole filter)
        if (inputDb > state.envelope)
            state.envelope = attackCoeff * state.envelope + (1.0f - attackCoeff) * inputDb;
        else
            state.envelope = releaseCoeff * state.envelope + (1.0f - releaseCoeff) * inputDb;

        // Calculate dynamic gain (uses soft-knee if enabled)
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
     * Thread-safe: Uses SpinLock to prevent race with updateDetectionFilter()
     */
    float processDetection(int bandIndex, float input, int channel)
    {
        if (bandIndex < 0 || bandIndex >= NUM_BANDS ||
            channel < 0 || channel >= static_cast<int>(detectionFilters.size()))
            return std::abs(input);

        // Use try-lock to avoid blocking the audio thread
        // If lock fails (coefficient update in progress), use unfiltered input
        const juce::SpinLock::ScopedTryLockType lock(filterLock);
        if (!lock.isLocked())
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
     * Implements soft-knee compression for smoother response.
     */
    float calculateDynamicGain(float envelopeDb, const BandParameters& params) const
    {
        float threshold = params.threshold;
        // Use atomic load for thread-safe access from audio thread
        float kneeWidth = softKneeEnabled.load(std::memory_order_acquire) ? params.kneeWidth : 0.0f;
        float halfKnee = kneeWidth / 2.0f;

        // Guard against division by zero - ensure ratio is at least 1.0
        float ratio = std::max(params.ratio, 1.0f);

        float reduction = 0.0f;

        if (envelopeDb < threshold - halfKnee)
        {
            // Below threshold (including knee) - no reduction
            reduction = 0.0f;
        }
        else if (envelopeDb > threshold + halfKnee || kneeWidth <= 0.0f)
        {
            // Above threshold + knee (hard compression) or hard knee mode
            float overThreshold = envelopeDb - threshold;
            reduction = overThreshold * (1.0f - 1.0f / ratio);
        }
        else
        {
            // In the soft knee region - quadratic interpolation
            float x = envelopeDb - threshold + halfKnee;
            float kneeGain = (x * x) / (2.0f * kneeWidth);
            reduction = kneeGain * (1.0f - 1.0f / ratio);
        }

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

    // SpinLock for thread-safe filter coefficient updates
    // Used between updateDetectionFilter() (message thread) and processDetection() (audio thread)
    mutable juce::SpinLock filterLock;

    // Atomic meters for UI (one per band, shows max across channels)
    std::array<std::atomic<float>, NUM_BANDS> dynamicGainMeters;

    // Lookahead buffers for peak detection (per channel, per band)
    struct LookaheadBuffer
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        float peakValue = 0.0f;
    };
    std::vector<std::vector<LookaheadBuffer>> lookaheadBuffers;

    // Global settings and state
    GlobalSettings globalSettings;
    std::atomic<int> lookaheadSamples{0};      // Thread-safe: written by message thread, read by audio thread
    std::atomic<bool> softKneeEnabled{true};   // Thread-safe: written by message thread, read by audio thread
    int numChannels = 2;

    double sampleRate = 44100.0;
};

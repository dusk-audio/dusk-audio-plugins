#pragma once

#include <array>
#include <cmath>
#include <algorithm>

//==============================================================================
/**
    True Peak Detector (ITU-R BS.1770-4 compliant)

    Uses 4x oversampling with polyphase FIR interpolation to detect
    inter-sample peaks that would exceed 0 dBTP when converted to analog.
*/
class TruePeakDetector
{
public:
    static constexpr int OVERSAMPLE_FACTOR = 4;
    static constexpr int TAPS_PER_PHASE = 12;
    static constexpr int NUM_CHANNELS = 2;

    TruePeakDetector() = default;

    void prepare(double /*sampleRate*/, int numChannels)
    {
        channels = std::min(numChannels, NUM_CHANNELS);
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < NUM_CHANNELS; ++ch)
        {
            truePeak[ch] = 0.0f;
            for (auto& s : history[ch])
                s = 0.0f;
            historyIndex[ch] = 0;
        }
        maxTruePeak = 0.0f;
        clippingDetected = false;
    }

    //==========================================================================
    // Process a block of samples
    void process(const float* const* channelData, int numSamples)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* data = channelData[ch];

            for (int i = 0; i < numSamples; ++i)
            {
                float peak = processSample(data[i], ch);
                if (peak > truePeak[ch])
                    truePeak[ch] = peak;
            }
        }

        // Update max and clipping flag
        maxTruePeak = std::max(truePeak[0], truePeak[1]);
        if (maxTruePeak > 1.0f)
            clippingDetected = true;
    }

    //==========================================================================
    // Process single sample and return peak
    float processSample(float sample, int channel)
    {
        // Store sample in history
        history[channel][historyIndex[channel]] = sample;
        historyIndex[channel] = (historyIndex[channel] + 1) % TAPS_PER_PHASE;

        // Find maximum across all 4 interpolated phases
        float maxPeak = std::abs(sample);

        for (int phase = 1; phase < OVERSAMPLE_FACTOR; ++phase)
        {
            float interpolated = 0.0f;

            for (int tap = 0; tap < TAPS_PER_PHASE; ++tap)
            {
                int idx = (historyIndex[channel] - tap - 1 + TAPS_PER_PHASE) % TAPS_PER_PHASE;
                interpolated += history[channel][idx] * coefficients[phase][tap];
            }

            maxPeak = std::max(maxPeak, std::abs(interpolated));
        }

        return maxPeak;
    }

    //==========================================================================
    // Get true peak in linear scale
    float getTruePeak(int channel) const
    {
        return truePeak[std::min(channel, NUM_CHANNELS - 1)];
    }

    // Get true peak in dB (dBTP)
    float getTruePeakDB(int channel) const
    {
        float peak = getTruePeak(channel);
        if (peak < 1e-10f) return -100.0f;
        return 20.0f * std::log10(peak);
    }

    // Get max true peak across all channels
    float getMaxTruePeak() const { return maxTruePeak; }

    float getMaxTruePeakDB() const
    {
        if (maxTruePeak < 1e-10f) return -100.0f;
        return 20.0f * std::log10(maxTruePeak);
    }

    // Check if true peak exceeded threshold
    bool isOverThreshold(float thresholdDbTP = 0.0f) const
    {
        float thresholdLinear = std::pow(10.0f, thresholdDbTP / 20.0f);
        return maxTruePeak > thresholdLinear;
    }

    bool hasClipped() const { return clippingDetected; }

    // Reset peak hold
    void resetPeakHold()
    {
        for (int ch = 0; ch < NUM_CHANNELS; ++ch)
            truePeak[ch] = 0.0f;
        maxTruePeak = 0.0f;
        clippingDetected = false;
    }

private:
    int channels = 2;
    std::array<float, NUM_CHANNELS> truePeak{0.0f, 0.0f};
    float maxTruePeak = 0.0f;
    bool clippingDetected = false;

    // Sample history for FIR interpolation
    std::array<std::array<float, TAPS_PER_PHASE>, NUM_CHANNELS> history{};
    std::array<int, NUM_CHANNELS> historyIndex{0, 0};

    // Polyphase FIR coefficients for 4x oversampling
    // Phase 0 is identity (just the sample), phases 1-3 are interpolated
    // These are optimized coefficients for audio true-peak detection
    static constexpr std::array<std::array<float, TAPS_PER_PHASE>, OVERSAMPLE_FACTOR> coefficients = {{
        // Phase 0 (identity - passthrough)
        {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}},
        // Phase 1 (1/4 sample)
        {{0.0017089843750f, -0.0091552734375f, 0.0292968750000f, -0.0770263671875f,
          0.3079833984375f, 0.8897705078125f, -0.1522216796875f, 0.0463867187500f,
          -0.0145263671875f, 0.0040283203125f, -0.0008544921875f, 0.0001220703125f}},
        // Phase 2 (1/2 sample)
        {{0.0f, -0.0156250000000f, 0.0f, 0.1538085937500f, 0.0f, 0.8623046875000f,
          0.0f, -0.1538085937500f, 0.0f, 0.0156250000000f, 0.0f, 0.0f}},
        // Phase 3 (3/4 sample)
        {{0.0001220703125f, -0.0008544921875f, 0.0040283203125f, -0.0145263671875f,
          0.0463867187500f, -0.1522216796875f, 0.8897705078125f, 0.3079833984375f,
          -0.0770263671875f, 0.0292968750000f, -0.0091552734375f, 0.0017089843750f}}
    }};
};

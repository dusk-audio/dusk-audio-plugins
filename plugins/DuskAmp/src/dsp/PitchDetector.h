// PitchDetector.h — autocorrelation-based monophonic pitch detector.
//
// Lightweight pitch tracker tuned for guitar. Runs once per audio block
// (allocation-free, ~10 µs typical). Buffers the last ~46 ms of input in
// a circular buffer and searches for the lag that maximises the
// autocorrelation; the lag-to-frequency conversion uses parabolic
// interpolation between adjacent integer lags for sub-sample accuracy.
//
// Range: ~50 Hz - 1500 Hz (covers low-B/drop-A through high-E + harmonics).
// If the input level is below kSilenceThreshold, the detector reports 0 Hz
// so the UI can show a "no signal" state instead of garbage at the noise
// floor.
//
// API contract:
//   prepare()       — allocates the circular buffer (NOT realtime-safe)
//   pushBlock()     — appends samples, runs detection, updates lastHz/Level
//   getLatestHz()   — reads the most recent detected frequency
//   getLatestLevel()— input RMS level in linear amplitude

#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

class PitchDetector
{
public:
    // Below this RMS, treat as silence and emit 0 Hz.
    static constexpr float kSilenceThreshold = 0.005f; // ~ -46 dBFS

    void prepare (double sampleRate, int historySamples = 2048)
    {
        sampleRate_ = sampleRate;
        history_.assign (static_cast<size_t> (historySamples), 0.0f);
        writePos_ = 0;
        latestHz_ = 0.0f;
        latestLevel_ = 0.0f;
        // Search range: 50..1500 Hz → lag range
        minLag_ = std::max (2, static_cast<int> (std::floor (sampleRate / 1500.0)));
        maxLag_ = std::min (static_cast<int> (history_.size()) - 2,
                            static_cast<int> (std::ceil  (sampleRate /   50.0)));
    }

    // Append one block, run a single detection at the end of it.
    // Allocation-free; safe to call from the audio thread.
    void pushBlock (const float* samples, int numSamples)
    {
        const int N = static_cast<int> (history_.size());
        if (N <= 0) return;

        // 1) Append into circular buffer + accumulate RMS.
        double sumSq = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = samples[i];
            history_[static_cast<size_t> (writePos_)] = s;
            writePos_ = (writePos_ + 1) % N;
            sumSq += static_cast<double> (s) * s;
        }
        latestLevel_ = numSamples > 0
            ? static_cast<float> (std::sqrt (sumSq / static_cast<double> (numSamples)))
            : 0.0f;

        // 2) Skip detection on silence.
        if (latestLevel_ < kSilenceThreshold)
        {
            latestHz_ = 0.0f;
            return;
        }

        // 3) Linearise the circular buffer into a stack-friendly read order
        //    (newest sample at index N-1) by indexing through the ring.
        // 4) Compute autocorrelation r(τ) for τ in [minLag, maxLag],
        //    using the latest N samples. r(τ) = Σ x[k]·x[k+τ].
        // 5) Find the τ with the largest r(τ) AFTER the first descending
        //    region — this avoids picking τ=0 (the trivial maximum).
        //
        // Using a difference-function variant (more robust than raw
        // autocorrelation for fundamentals near the buffer's lower edge):
        //   d(τ) = Σ (x[k] - x[k+τ])²  — small at the period
        //   The CMNDF (cumulative-mean-normalised difference function) gives
        //   the YIN algorithm's robustness without a ton of extra cost.
        const int historyLen = N;
        // Use the most recent (historyLen - maxLag) samples as the analysis
        // frame so x[k+τ] never wraps past the latest write.
        const int frameLen = historyLen - maxLag_;

        auto readAt = [&] (int idx) noexcept -> float
        {
            // idx is "samples ago, where 0 = oldest"; map to circular pos.
            // The OLDEST sample is at writePos_ (since the buffer is full
            // after the first prepare()-sized push).
            int p = writePos_ + idx;
            if (p >= historyLen) p -= historyLen;
            return history_[static_cast<size_t> (p)];
        };

        // Difference function with running cumulative-mean normalisation.
        float runningCMND = 0.0f;
        int   bestTau    = -1;
        float bestValue  = std::numeric_limits<float>::max();
        constexpr float kCMNDThreshold = 0.15f; // YIN's "harmonic threshold"

        for (int tau = minLag_; tau <= maxLag_; ++tau)
        {
            float d = 0.0f;
            for (int k = 0; k < frameLen; ++k)
            {
                const float a = readAt (k);
                const float b = readAt (k + tau);
                const float diff = a - b;
                d += diff * diff;
            }
            runningCMND += d;
            const float meanD = runningCMND / static_cast<float> (tau - minLag_ + 1);
            const float cmnd  = (meanD > 0.0f) ? d / meanD : 1.0f;

            if (cmnd < kCMNDThreshold && cmnd < bestValue)
            {
                bestValue = cmnd;
                bestTau   = tau;
                // Early-out as soon as we're confident.
                break;
            }
            if (cmnd < bestValue)
            {
                bestValue = cmnd;
                bestTau   = tau;
            }
        }

        if (bestTau < 0)
        {
            latestHz_ = 0.0f;
            return;
        }

        // 6) Parabolic interpolation between τ-1, τ, τ+1 for sub-sample
        //    accuracy on the cents reading.
        float fracTau = static_cast<float> (bestTau);
        if (bestTau > minLag_ && bestTau < maxLag_)
        {
            // Re-evaluate d at the three neighbours.
            auto diffAt = [&] (int tau) noexcept
            {
                float dd = 0.0f;
                for (int k = 0; k < frameLen; ++k)
                {
                    const float a = readAt (k);
                    const float b = readAt (k + tau);
                    const float dif = a - b;
                    dd += dif * dif;
                }
                return dd;
            };
            const float yPrev = diffAt (bestTau - 1);
            const float yCurr = diffAt (bestTau);
            const float yNext = diffAt (bestTau + 1);
            const float denom = yPrev - 2.0f * yCurr + yNext;
            if (std::abs (denom) > 1.0e-12f)
            {
                const float delta = 0.5f * (yPrev - yNext) / denom;
                fracTau = static_cast<float> (bestTau) + std::clamp (delta, -1.0f, 1.0f);
            }
        }

        latestHz_ = static_cast<float> (sampleRate_ / static_cast<double> (fracTau));
    }

    float getLatestHz()    const noexcept { return latestHz_; }
    float getLatestLevel() const noexcept { return latestLevel_; }

private:
    double sampleRate_ = 44100.0;
    std::vector<float> history_;
    int writePos_  = 0;
    int minLag_    = 30;
    int maxLag_    = 880;

    float latestHz_    = 0.0f;
    float latestLevel_ = 0.0f;
};

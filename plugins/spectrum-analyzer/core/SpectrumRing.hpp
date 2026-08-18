// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// SpectrumRing.hpp - single-producer single-consumer sample ring for the
// spectrum analyzer's audio-to-UI handoff.
//
// Framework-free replacement for the juce::AbstractFifo pair FFTProcessor used
// to use, with the same split-region protocol: the writer reserves up to N
// slots (possibly wrapping, hence two regions), copies, then commits; the
// reader mirrors that. Capacity semantics match AbstractFifo: one slot is
// sacrificed so full and empty are distinguishable, and a write larger than
// the free space is truncated rather than blocking, which is the correct
// policy for a display feed (drop, never stall the audio thread).
//
// Thread contract: exactly one writer thread (audio) and one reader thread
// (UI/timer). write index is released by the producer and acquired by the
// consumer; read index the other way around. No locks, no allocation after
// prepare().
#pragma once

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

class SpectrumRing
{
public:
    explicit SpectrumRing(int capacity = 16384) { setCapacity(capacity); }

    // Not thread-safe; call before the threads start (prepare/reset time).
    void setCapacity(int capacity)
    {
        cap = std::max(2, capacity);
        buffer.assign((size_t)cap, 0.0f);
        readPos.store(0, std::memory_order_relaxed);
        writePos.store(0, std::memory_order_relaxed);
    }

    // Not thread-safe; pair with the audio thread stopped (reset()).
    void clear()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        readPos.store(0, std::memory_order_relaxed);
        writePos.store(0, std::memory_order_relaxed);
    }

    // Samples the consumer could read right now.
    int numReady() const
    {
        const int w = writePos.load(std::memory_order_acquire);
        const int r = readPos.load(std::memory_order_relaxed);
        return w >= r ? w - r : w + cap - r;
    }

    // Producer: append up to numSamples (truncating on a full ring).
    void write(const float* src, int numSamples)
    {
        const int w = writePos.load(std::memory_order_relaxed);
        const int r = readPos.load(std::memory_order_acquire);
        const int used = w >= r ? w - r : w + cap - r;
        const int free = cap - 1 - used;
        const int n = std::min(numSamples, free);
        if (n <= 0)
            return;

        const int first = std::min(n, cap - w);
        std::memcpy(buffer.data() + w, src, (size_t)first * sizeof(float));
        if (n > first)
            std::memcpy(buffer.data(), src + first, (size_t)(n - first) * sizeof(float));

        writePos.store((w + n) % cap, std::memory_order_release);
    }

    // Consumer: copy exactly numSamples into dst and consume them.
    // Returns false (touching nothing) when fewer are available.
    bool read(float* dst, int numSamples)
    {
        if (numReady() < numSamples)
            return false;

        const int r = readPos.load(std::memory_order_relaxed);
        const int first = std::min(numSamples, cap - r);
        std::memcpy(dst, buffer.data() + r, (size_t)first * sizeof(float));
        if (numSamples > first)
            std::memcpy(dst + first, buffer.data(), (size_t)(numSamples - first) * sizeof(float));

        readPos.store((r + numSamples) % cap, std::memory_order_release);
        return true;
    }

private:
    int cap = 2;
    std::vector<float> buffer;
    std::atomic<int> readPos { 0 };
    std::atomic<int> writePos { 0 };
};

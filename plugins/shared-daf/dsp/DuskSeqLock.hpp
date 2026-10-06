// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskSeqLock.hpp — single-writer, many-reader "latest value" publication for a
// trivially copyable struct, without blocking the writer.
//
// The audio thread publishes a whole frame of data (a magnitude spectrum, a
// filter table, a meter block) and the UI wants the most recent complete one.
// SpectrumRing (plugins/spectrum-analyzer/core) is the stream-shaped answer to
// that problem: every sample reaches the reader, in order. This is the
// snapshot-shaped answer: the reader only ever wants the newest frame, and a
// frame it never saw is no loss. A seqlock gives exactly that: the writer never
// waits, a reader that overlaps a write simply retries.
//
// Writer: exactly one thread calls store(). Readers: any number of threads call
// load(); each load is a full copy of T, so keep T to tens of KB at most.
//
// T must be trivially copyable: the reader copies it while the writer may be
// half-way through overwriting it, and then throws the copy away if the
// sequence moved. A type with pointers or invariants cannot survive that.
#pragma once

#include <atomic>
#include <cstring>
#include <type_traits>

namespace duskaudio
{

template <typename T>
class SeqLock
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "SeqLock<T> copies T byte-wise while it may be torn; T must be trivially copyable");

public:
    SeqLock() noexcept : value_{} {}

    // Writer only. Odd sequence = write in progress.
    void store(const T& v) noexcept
    {
        const unsigned s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(static_cast<void*>(&value_), &v, sizeof(T));
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(s + 2, std::memory_order_release);
    }

    // Any thread. Returns false if no frame has ever been stored, or if the
    // writer stayed mid-store for the whole retry budget: a reader must never
    // spin for as long as a parked audio thread (host deactivation, a debugger)
    // keeps the sequence odd. On false the caller keeps its previous frame.
    bool load(T& out, int maxAttempts = 64) const noexcept
    {
        // Read into a local first: `out` is only touched by a read that
        // validated, so a torn attempt can never leave half a frame behind.
        T local;
        for (int attempt = 0; attempt < maxAttempts; ++attempt)
        {
            const unsigned s0 = seq_.load(std::memory_order_acquire);
            if (s0 == 0)
                return false;
            if (s0 & 1u)
                continue;                         // writer mid-store
            std::memcpy(static_cast<void*>(&local), &value_, sizeof(T));
            std::atomic_thread_fence(std::memory_order_acquire);
            const unsigned s1 = seq_.load(std::memory_order_relaxed);
            if (s0 == s1)
            {
                std::memcpy(static_cast<void*>(&out), &local, sizeof(T));
                return true;
            }
        }
        return false;
    }

    // Sequence number of the newest complete frame (even, 0 = none yet). A
    // reader that polls at 60 Hz can skip the copy when this has not moved.
    unsigned sequence() const noexcept
    {
        const unsigned s = seq_.load(std::memory_order_acquire);
        return s & ~1u;
    }

private:
    std::atomic<unsigned> seq_ { 0 };
    T value_;
};

} // namespace duskaudio

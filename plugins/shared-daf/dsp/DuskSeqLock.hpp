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
// T must be trivially copyable. Its object representation is published as
// lock-free atomic words: concurrent non-atomic memcpy would be a C++ data race,
// even when the sequence check subsequently discarded the torn copy.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace duskaudio
{

template <typename T>
class SeqLock
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "SeqLock<T> copies T byte-wise while it may be torn; T must be trivially copyable");
    using Word = std::uint32_t;
    static_assert(std::atomic<Word>::is_always_lock_free,
                  "SeqLock payload publication must never lock the audio thread");
    static constexpr size_t kWordCount = (sizeof(T) + sizeof(Word) - 1) / sizeof(Word);

public:
    SeqLock() noexcept
    {
        const T initial {};
        const auto* src = reinterpret_cast<const unsigned char*>(&initial);
        for (size_t offset = 0; offset < sizeof(T); offset += sizeof(Word))
        {
            Word word = 0;
            const size_t count = sizeof(T) - offset < sizeof(Word) ? sizeof(T) - offset : sizeof(Word);
            std::memcpy(&word, src + offset, count);
            words_[offset / sizeof(Word)].store(word, std::memory_order_relaxed);
        }
    }

    // Writer only. Odd sequence = write in progress. `bytes` lets a frame whose
    // payload is only partly in use (a spectrum sized for the largest FFT)
    // publish just its leading `bytes`; the reader must ask for the same.
    void store(const T& v, size_t bytes = sizeof(T)) noexcept
    {
        bytes = bytes > sizeof(T) ? sizeof(T) : bytes;
        const unsigned s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);   // the odd mark lands before the data
        const auto* src = reinterpret_cast<const unsigned char*>(&v);
        for (size_t offset = 0; offset < bytes; offset += sizeof(Word))
        {
            const size_t count = bytes - offset < sizeof(Word) ? bytes - offset : sizeof(Word);
            // Preserve the suffix of a partially published word, just as a
            // short memcpy preserved the unused tail of the previous frame.
            Word word = count < sizeof(Word) ? words_[offset / sizeof(Word)].load(std::memory_order_relaxed) : 0;
            std::memcpy(&word, src + offset, count);
            words_[offset / sizeof(Word)].store(word, std::memory_order_relaxed);
        }
        seq_.store(s + 2, std::memory_order_release);           // orders the data before the even mark
    }

    // Any thread. Returns false if no frame has ever been stored, or if the
    // writer stayed mid-store for the whole retry budget: a reader must never
    // spin for as long as a parked audio thread (host deactivation, a debugger)
    // keeps the sequence odd. On false the caller keeps its previous frame.
    bool load(T& out, size_t bytes = sizeof(T), int maxAttempts = 64) const noexcept
    {
        bytes = bytes > sizeof(T) ? sizeof(T) : bytes;
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
            auto* dst = reinterpret_cast<unsigned char*>(&local);
            for (size_t offset = 0; offset < bytes; offset += sizeof(Word))
            {
                const Word word = words_[offset / sizeof(Word)].load(std::memory_order_relaxed);
                const size_t count = bytes - offset < sizeof(Word) ? bytes - offset : sizeof(Word);
                std::memcpy(dst + offset, &word, count);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            const unsigned s1 = seq_.load(std::memory_order_relaxed);
            if (s0 == s1)
            {
                std::memcpy(static_cast<void*>(&out), &local, bytes);
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
    std::atomic<Word> words_[kWordCount];
};

} // namespace duskaudio

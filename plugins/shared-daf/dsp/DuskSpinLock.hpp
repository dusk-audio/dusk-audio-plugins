// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskSpinLock.hpp — a test-and-set spinlock whose audio-thread side never
// blocks.
//
// Framework-free counterpart of juce::SpinLock. The rule it exists to enforce is
// the one CLAUDE.md states for every processBlock: the audio thread may only
// TRY to take a lock and must have a plan for failing. The other threads (host,
// UI) spin until they get it; their critical sections are a few hundred bytes of
// copying, so the wait is bounded by how long the audio thread holds the lock,
// which is never longer than one of those copies either.
//
//   duskaudio::SpinLock lock;
//   // host / UI thread
//   { duskaudio::SpinLock::ScopedLock guard(lock); table = edited; }
//   // audio thread
//   { duskaudio::SpinLock::ScopedTryLock guard(lock);
//     if (guard.isLocked()) { live = table; } /* else: use last copy */ }
//
// Not reentrant. Not fair. Both are fine for a two-party handoff.
#pragma once

#include <atomic>
#include <thread>

#if defined(_MSC_VER)
  #include <intrin.h>
  #if defined(_M_ARM64) || defined(_M_ARM)
    #define DUSK_SPIN_PAUSE() __yield()
  #else
    #define DUSK_SPIN_PAUSE() _mm_pause()
  #endif
#elif defined(__SSE2__) || defined(__x86_64__)
  #include <emmintrin.h>
  #define DUSK_SPIN_PAUSE() _mm_pause()
#elif defined(__aarch64__)
  #define DUSK_SPIN_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
  #define DUSK_SPIN_PAUSE() ((void)0)
#endif

namespace duskaudio
{

class SpinLock
{
public:
    SpinLock() noexcept = default;
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    bool tryLock() noexcept
    {
        return !flag_.test_and_set(std::memory_order_acquire);
    }

    // Never called from the audio thread (that side uses tryLock). A pause hint
    // keeps a spinning sibling hyperthread from stealing issue slots from the
    // holder it is waiting on, and after a few spins the wait yields the core.
    void lock() noexcept
    {
        int spins = 0;
        while (flag_.test_and_set(std::memory_order_acquire))
        {
            if (++spins < 64)
                DUSK_SPIN_PAUSE();
            else
                std::this_thread::yield();
        }
    }

    void unlock() noexcept { flag_.clear(std::memory_order_release); }

    class ScopedLock
    {
    public:
        explicit ScopedLock(SpinLock& l) noexcept : lock_(l) { lock_.lock(); }
        ~ScopedLock() noexcept { lock_.unlock(); }
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;
    private:
        SpinLock& lock_;
    };

    class ScopedTryLock
    {
    public:
        explicit ScopedTryLock(SpinLock& l) noexcept : lock_(l), locked_(l.tryLock()) {}
        ~ScopedTryLock() noexcept { if (locked_) lock_.unlock(); }
        ScopedTryLock(const ScopedTryLock&) = delete;
        ScopedTryLock& operator=(const ScopedTryLock&) = delete;
        bool isLocked() const noexcept { return locked_; }
    private:
        SpinLock& lock_;
        bool locked_;
    };

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

} // namespace duskaudio

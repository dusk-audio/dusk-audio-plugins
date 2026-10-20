// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: reading learned spectra must not race the audio thread's next publish.
#include "../core/MultiQMatch.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <thread>

#if defined(_MSC_VER)
#define MATCH_NOINLINE __declspec(noinline)
#else
#define MATCH_NOINLINE __attribute__((noinline))
#endif

namespace duskaudio
{
struct MatchSnapshotTestAccess
{
    template <typename T>
    static void pinSpares(MatchSnapshot<T>& snapshots, bool pin)
    {
        const uint32_t current = snapshots.published_.load();
        for (uint32_t i = 0; i < snapshots.slots_.size(); ++i)
            if (i != current) snapshots.slots_[i].users.store(pin ? 1u : 0u);
    }
};
}

namespace
{
using Frame = std::array<unsigned, 4096>;
using Snapshot = duskaudio::MatchSnapshot<Frame>;

// Keep the actual payload accesses in separate functions. In the control
// experiment, inlining let Clang 19 omit their ThreadSanitizer instrumentation;
// this form reports the old publication race as well as detecting torn frames.
MATCH_NOINLINE void fill(unsigned* words, unsigned generation)
{
    for (unsigned i = 0; i < 4096; ++i) words[i] = generation;
}
MATCH_NOINLINE unsigned first(const unsigned* words) { return words[0]; }
MATCH_NOINLINE bool coherent(const unsigned* words, unsigned generation)
{
    for (unsigned i = 0; i < 4096; ++i) if (words[i] != generation) return false;
    return true;
}
}

int main()
{
    Snapshot snapshots;
    fill(snapshots.writeBuffer().data(), 1); snapshots.publish();
    bool stable;
    {
        const auto& frame = snapshots.readBuffer();
        fill(snapshots.writeBuffer().data(), 2); snapshots.publish();
        fill(snapshots.writeBuffer().data(), 3); snapshots.publish();
        stable = coherent(frame.data(), 1);
    }
    if (!stable) std::fputs("FAIL: a returned frame changed after two later publications\n", stderr);

    // Model two readers parked while copying older slots. Display updates may
    // be dropped, but reset/clear must immediately make old learned data absent.
    duskaudio::MatchSnapshotTestAccess::pinSpares(snapshots, true);
    snapshots.clear();
    const auto cleared = snapshots.readBuffer();
    const bool reset = coherent(cleared.data(), 0);
    if (!reset) std::fputs("FAIL: clear retained an old frame while readers pinned spare slots\n", stderr);
    duskaudio::MatchSnapshotTestAccess::pinSpares(snapshots, false);

    std::atomic<unsigned> ready{0}, totalReads{0}, totalTorn{0};
    std::atomic<bool> done{false};
    std::thread producer([&] {
        while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
        for (unsigned generation = 4; generation < 4004; ++generation)
        {
            fill(snapshots.writeBuffer().data(), generation);
            snapshots.publish();
        }
        done.store(true, std::memory_order_release);
    });
    const auto consume = [&] {
        unsigned reads = 0, torn = 0;
        ready.fetch_add(1, std::memory_order_release);
        do
        {
            const auto& frame = snapshots.readBuffer();
            const unsigned generation = first(frame.data());
            std::this_thread::yield();
            if (!coherent(frame.data(), generation)) ++torn;
            ++reads;
        } while (!done.load(std::memory_order_acquire));
        totalReads.fetch_add(reads, std::memory_order_relaxed);
        totalTorn.fetch_add(torn, std::memory_order_relaxed);
    };
    std::thread secondReader(consume);
    consume();
    producer.join();
    secondReader.join();
    // A writer may drop a display publication if all spare slots are pinned.
    // Once readers quiesce it can publish the complete last accumulated frame.
    snapshots.publish();
    const auto& latest = snapshots.readBuffer();
    const bool current = coherent(latest.data(), 4003);
    const unsigned reads = totalReads.load(), torn = totalTorn.load();
    std::printf("Match snapshots: stable=%d, reset=%d, reads=%u, torn=%u, latest=%d\n",
                stable, reset, reads, torn, current);
    return stable && reset && reads > 0 && torn == 0 && current ? 0 : 1;
}

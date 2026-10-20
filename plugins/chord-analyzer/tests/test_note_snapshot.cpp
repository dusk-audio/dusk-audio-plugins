// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "AllocationProbe.hpp"
#include "../core/NoteSnapshot.hpp"
#include <atomic>
#include <cstdio>
#include <thread>

int main()
{
    using namespace duskaudio::chord;
    int failures = 0;
    const auto check = [&](bool ok, const char* message)
    {
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
    };
    const NoteSnapshot a{{UINT64_C(0x8000000000000001), UINT64_C(0x8000000000000001)}, 1.25};
    const NoteSnapshot b{{0, 0}, 2.5};
    const auto equal = [](const NoteSnapshot& x, const NoteSnapshot& y)
    { return x.noteWords == y.noteWords && x.timeSec == y.timeSec; };
    NoteSnapshotMailbox mailbox;
    NoteSnapshot out = a;
    check(!mailbox.load(out) && equal(out, a), "unpublished mailbox preserves the caller's frame");
    check(ChordAllocationProbe::positiveControl(), "allocation probe observes a real request");
    ChordAllocationProbe::watchAllocations = true;
    mailbox.store(b);
    const bool loaded = mailbox.load(out);
    ChordAllocationProbe::watchAllocations = false;
    check(loaded && equal(out, b) && ChordAllocationProbe::allocationCalls == 0,
          "publication/read copies the complete timed note frame without allocation");
    check(!mailbox.load(out, sizeof(out), 0) && equal(out,b), "bounded read miss preserves the previous frame");

    std::atomic<unsigned> phase{0}, acknowledgement{0};
    std::atomic<bool> go{false}, done{false};
    std::size_t writerAllocations = 0;
    std::thread writer([&]
    {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        ChordAllocationProbe::allocationCalls = 0;
        ChordAllocationProbe::watchAllocations = true;
        for (int i = 0; i < 100000; ++i) mailbox.store(i % 2 ? a : b);
        mailbox.store(a); phase.store(1, std::memory_order_release);
        while (acknowledgement.load(std::memory_order_acquire) < 1) std::this_thread::yield();
        mailbox.store(b); phase.store(2, std::memory_order_release);
        while (acknowledgement.load(std::memory_order_acquire) < 2) std::this_thread::yield();
        ChordAllocationProbe::watchAllocations = false;
        writerAllocations = ChordAllocationProbe::allocationCalls;
        done.store(true, std::memory_order_release);
    });
    go.store(true, std::memory_order_release);
    unsigned accepted = 0, rejected = 0, torn = 0;
    do
    {
        const auto previous = out;
        if (mailbox.load(out))
        {
            ++accepted;
            if (!equal(out,a) && !equal(out,b)) ++torn;
        }
        else { ++rejected; if (!equal(out,previous)) ++torn; }
        const unsigned current = phase.load(std::memory_order_acquire);
        if (current == 1 && equal(out,a)) acknowledgement.store(1, std::memory_order_release);
        if (current == 2 && equal(out,b)) acknowledgement.store(2, std::memory_order_release);
    } while (!done.load(std::memory_order_acquire));
    writer.join();
    check(accepted >= 2 && acknowledgement.load() == 2, "reader actually observes both explicitly published boundary states");
    check(torn == 0, "note words and transition time never mix publications");
    check(writerAllocations == 0, "all concurrent audio-writer publications allocate nothing");
    std::printf("Note snapshots: %u accepted, %u bounded misses, %u torn, %d failures\n", accepted, rejected, torn, failures);
    return failures == 0 ? 0 : 1;
}

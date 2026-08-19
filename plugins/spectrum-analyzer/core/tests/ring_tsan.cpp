// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// Race-detector harness for SpectrumRing (GH #184; the command lives in
// docs/dpf-migration/10-spectrum-analyzer.md):
//
//   clang++ -std=c++17 -O1 -g -fsanitize=thread \
//       plugins/spectrum-analyzer/core/tests/ring_tsan.cpp -o ring_tsan && ./ring_tsan
//
// A writer thread pushes audio-rate blocks while a reader snapshots at display
// rate, exactly the production thread shape. Two gates:
//   1. TSan reports nothing (the process exits 0 under -fsanitize=thread).
//   2. No torn read: the writer emits a strictly increasing ramp. The ring
//      DROPS samples when full (write truncation is its design), so forward
//      jumps are legal; what a torn, duplicated, or reordered copy produces
//      is a NON-INCREASING step, and that fails the run even without TSan.
// Also runnable without TSan as a plain stress test (gate 2 still applies).

#include "../SpectrumRing.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    SpectrumRing ring(16384);
    std::atomic<bool> stop { false };
    std::atomic<long> torn { 0 };
    std::atomic<long> drops { 0 };
    std::atomic<long> blocksRead { 0 };

    // Writer: 512-sample blocks of a continuous ramp, ~audio pacing. The ramp
    // value is encoded so consecutiveness is checkable on the far side.
    std::thread writer([&] {
        float next = 0.0f;
        float block[512];
        while (!stop.load(std::memory_order_relaxed))
        {
            for (int i = 0; i < 512; ++i) { block[i] = next; next += 1.0f;
                                            if (next > 1e7f) next = 0.0f; }
            ring.write(block, 512);
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    });

    // Reader: display pace, consuming FFT-sized chunks like processFFT().
    std::thread reader([&] {
        float buf[4096];
        while (!stop.load(std::memory_order_relaxed))
        {
            if (ring.numReady() >= 4096 && ring.read(buf, 4096))
            {
                blocksRead.fetch_add(1, std::memory_order_relaxed);
                for (int i = 1; i < 4096; ++i)
                {
                    const float step = buf[i] - buf[i - 1];
                    const bool wrap = buf[i] < 2.0f && buf[i - 1] > 1e7f - 514.0f;
                    if (step == 1.0f || wrap)
                        continue;
                    if (step > 1.0f)
                        drops.fetch_add(1, std::memory_order_relaxed);   // legal truncation
                    else
                    {
                        torn.fetch_add(1, std::memory_order_relaxed);    // duplicate/reorder
                        break;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(10));
    stop.store(true);
    writer.join();
    reader.join();

    std::printf("blocks read: %ld, torn: %ld, legal drops: %ld\n",
                blocksRead.load(), torn.load(), drops.load());
    if (blocksRead.load() == 0) { std::printf("FAIL: reader starved\n"); return 1; }
    if (torn.load() != 0)       { std::printf("FAIL: torn reads\n");    return 1; }
    std::printf("RING OK\n");
    return 0;
}

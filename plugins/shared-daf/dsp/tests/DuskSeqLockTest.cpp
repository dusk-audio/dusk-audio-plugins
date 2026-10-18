// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Run under ThreadSanitizer as well as normally: a seqlock can return coherent
// snapshots while still containing forbidden concurrent non-atomic accesses.
#include "DuskSeqLock.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

static int failures = 0;

#define CHECK(condition, message) \
    do { if (!(condition)) { ++failures; std::printf("FAIL: %s\n", message); } } while (false)

static void testPartialFrames()
{
    using Frame = std::array<unsigned char, 11>; // Includes a partial final word.
    duskaudio::SeqLock<Frame> snapshot;
    Frame out;
    out.fill(0xa5);
    const Frame untouched = out;
    CHECK(!snapshot.load(out) && out == untouched, "no publication leaves the destination untouched");
    CHECK(snapshot.sequence() == 0, "initial sequence is zero");

    Frame original;
    original.fill(0x17);
    snapshot.store(original);
    CHECK(snapshot.sequence() == 2, "a complete publication advances the sequence");
    CHECK(snapshot.load(out) && out == original, "the whole frame round trips including its last word");

    Frame next;
    next.fill(0x83);
    // Every alignment, including empty and oversized requests. Short stores
    // preserve previously published bytes beyond the prefix.
    for (size_t bytes = 0; bytes <= next.size() + 2; ++bytes)
    {
        snapshot.store(original);
        snapshot.store(next, bytes);
        Frame expected = original;
        for (size_t i = 0; i < bytes && i < expected.size(); ++i) expected[i] = next[i];
        CHECK(snapshot.load(out) && out == expected, "a short store preserves the suffix");

        out = untouched;
        Frame expectedRead = untouched;
        for (size_t i = 0; i < bytes && i < expectedRead.size(); ++i) expectedRead[i] = expected[i];
        CHECK(snapshot.load(out, bytes) && out == expectedRead, "a short load only replaces the prefix");
    }
    out = untouched;
    CHECK(!snapshot.load(out, sizeof(out), 0) && out == untouched,
          "an exhausted retry budget leaves the destination untouched");
}

static void testConcurrentFrames()
{
    using Frame = std::array<std::uint32_t, 1024>;
    duskaudio::SeqLock<Frame> snapshot;
    Frame frame;
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = (std::uint32_t)i;
    snapshot.store(frame);

    constexpr int readerCount = 3;
    std::atomic<int> ready { 0 };
    std::atomic<bool> start { false }, done { false };
    std::array<unsigned, readerCount> reads {};
    std::array<unsigned, readerCount> torn {};
    std::array<std::thread, readerCount> readers;
    for (int r = 0; r < readerCount; ++r)
        readers[(size_t)r] = std::thread([&, r]
        {
            Frame out;
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            do
            {
                if (snapshot.load(out))
                {
                    ++reads[(size_t)r];
                    for (size_t i = 1; i < out.size(); ++i)
                        if (out[i] != out[0] + (std::uint32_t)i) { ++torn[(size_t)r]; break; }
                }
            } while (!done.load(std::memory_order_acquire));
            // Also check the final frame, guaranteeing a validated read even
            // if a scheduler ran the writer for the entire publication burst.
            if (snapshot.load(out))
            {
                ++reads[(size_t)r];
                for (size_t i = 0; i < out.size(); ++i)
                    if (out[i] != 10000u + (std::uint32_t)i) { ++torn[(size_t)r]; break; }
            }
        });
    while (ready.load(std::memory_order_acquire) != readerCount) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (std::uint32_t generation = 1; generation <= 10000; ++generation)
    {
        for (size_t i = 0; i < frame.size(); ++i) frame[i] = generation + (std::uint32_t)i;
        snapshot.store(frame);
        if (generation % 32 == 0) std::this_thread::yield();
    }
    done.store(true, std::memory_order_release);
    for (auto& reader : readers) reader.join();
    for (int r = 0; r < readerCount; ++r)
    {
        CHECK(reads[(size_t)r] > 0, "each reader validates a frame");
        CHECK(torn[(size_t)r] == 0, "validated frames contain exactly one generation");
    }
}

int main()
{
    testPartialFrames();
    testConcurrentFrames();
    std::printf("DuskSeqLockTest: %s\n", failures == 0 ? "all checks passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}

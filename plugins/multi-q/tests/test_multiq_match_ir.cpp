// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: publishing another correction must not overwrite an IR being convolved.
#include "../core/MultiQMatch.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Match = duskaudio::MultiQMatch;
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

// A valid MQM1 fixture with an exact scaled impulse, independent of FIR design.
std::string state(float gain, bool active = true, bool learned = false)
{
    std::vector<uint8_t> bytes;
    auto put = [&](auto value) {
        const auto* first = reinterpret_cast<const uint8_t*>(&value);
        bytes.insert(bytes.end(), first, first + sizeof(value));
    };
    for (char c : std::string("MQM1")) put(c);
    for (int32_t value : {learned ? 1 : 0, learned ? 3 : 0, learned ? 1 : 0,
                          learned ? 3 : 0, active ? 1 : 0}) put(value);
    for (int i = 0; i < 2 * Match::NUM_BINS; ++i) put(learned ? 1.0 : 0.0);
    for (int i = 0; i < Match::NUM_BINS; ++i) put(0.0f);
    for (int i = 0; i < Match::FIR_LENGTH; ++i) put(i == 0 ? gain : 0.0f);
    constexpr const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;
    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        const uint32_t word = (uint32_t{bytes[i]} << 16)
            | (i + 1 < bytes.size() ? uint32_t{bytes[i + 1]} << 8 : 0u)
            | (i + 2 < bytes.size() ? bytes[i + 2] : 0u);
        text += alphabet[(word >> 18) & 63]; text += alphabet[(word >> 12) & 63];
        text += i + 1 < bytes.size() ? alphabet[(word >> 6) & 63] : '=';
        text += i + 2 < bytes.size() ? alphabet[word & 63] : '=';
    }
    return text;
}

float render(Match& match, bool stereo, int blocks = 20)
{
    std::array<float, 256> left{}, right{};
    for (int block = 0; block < blocks; ++block)
    {
        left.fill(0.25f); right.fill(-0.125f);
        match.process(left.data(), stereo ? right.data() : nullptr, stereo, int(left.size()));
    }
    if (stereo && std::abs(right.back() + 0.5f * left.back()) > 1e-5f)
        return -100.0f;
    return left.back();
}

void blockOwnership(bool stereo)
{
    auto match = std::make_unique<Match>(); match->prepare(48000.0, 256);
    check(match->deserialize(state(0.5f)), "initial scaled-impulse state loads");
    match->audioThreadSync();
    check(std::abs(render(*match, stereo) - 0.125f) < 1e-5f, "initial correction really processes audio");
    check(match->deserialize(state(1.0f)) && match->deserialize(state(2.0f)), "two newer corrections queue");
    check(std::abs(render(*match, stereo) - 0.125f) < 1e-5f,
          "queued updates cannot change the audio-owned IR before the next sync");
    match->audioThreadSync();
    check(std::abs(render(*match, stereo) - 0.5f) < 1e-5f, "the next sync adopts the latest complete IR");
    check(match->getLatencySamples() == Match::CONV_HOP, "replacement retains convolution latency");

    check(match->deserialize(state(0.5f)) && match->deserialize(state(0.0f, false)), "deactivation supersedes activation");
    match->audioThreadSync();
    check(std::abs(render(*match, stereo) - 0.25f) < 1e-5f && match->getLatencySamples() == 0,
          "deactivation fades the owned IR then removes latency");
    check(match->deserialize(state(0.5f)), "activation can queue after deactivation");
    match->requestClear(); match->audioThreadSync();
    check(std::abs(render(*match, stereo) - 0.25f) < 1e-5f && match->getLatencySamples() == 0,
          "clear discards an older queued activation");

    check(match->deserialize(state(2.0f)), "activation can queue before reset");
    match->reset(); match->audioThreadSync();
    check(std::abs(render(*match, stereo) - 0.25f) < 1e-5f && match->getLatencySamples() == 0,
          "reset discards queued IRs");
    match->prepare(48000.0, 256); match->prepare(48000.0, 256);
    check(match->deserialize(state(0.5f)), "repeated prepare permits a fresh correction");
    match->audioThreadSync();
    check(std::abs(render(*match, stereo) - 0.125f) < 1e-5f, "fresh correction works after repeated prepare");
    match->process(nullptr, nullptr, false, 0);
}

void concurrentReplacement()
{
    auto match = std::make_unique<Match>(); match->prepare(48000.0, 256);
    const std::string low = state(0.5f), high = state(2.0f);
    check(match->deserialize(low), "concurrent fixture loads"); match->audioThreadSync();
    check(std::abs(render(*match, true) - 0.125f) < 1e-5f, "concurrent fixture completes its fade-in");
    std::atomic<bool> running{true}, ready{false};
    std::atomic<int> rendered{0}, nonfinite{0}, mixed{0};
    std::thread audio([&] {
        // Long blocks keep an IR in use across several producer publications.
        std::array<float, 65536> left{}, right{};
        ready.store(true, std::memory_order_release);
        do
        {
            match->audioThreadSync(); left.fill(0.25f); right.fill(-0.125f);
            match->process(left.data(), right.data(), true, int(left.size()));
            for (float value : left) if (!std::isfinite(value)) ++nonfinite;
            // Ignore FIFO/partition boundary history. A delta IR and constant
            // input must then stay at the one gain adopted for this block.
            bool changed = false;
            for (size_t i = 1024; i < left.size(); ++i)
                changed |= std::abs(left[i] - left.back()) > 1e-5f;
            if (changed) ++mixed;
            ++rendered;
        } while (running.load(std::memory_order_acquire));
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    bool accepted = true;
    for (int i = 0; i < 100; ++i) accepted &= match->deserialize(i & 1 ? high : low);
    running.store(false, std::memory_order_release); audio.join();
    check(accepted && rendered > 0 && nonfinite == 0, "live IR replacement processes finite audio concurrently");
    check(mixed == 0, "one audio block cannot mix multiple published correction IRs");
    match->audioThreadSync();
    check(std::abs(render(*match, true) - 0.5f) < 1e-5f, "latest IR survives concurrent replacements");
    std::printf("Live IR replacements: %d audio blocks, %d mixed blocks, %d nonfinite samples\n",
                rendered.load(), mixed.load(), nonfinite.load());
}

void phaseMetadata()
{
    auto match = std::make_unique<Match>(); match->prepare(48000.0, 256);
    check(match->deserialize(state(0.5f, true, true)), "phase fixture has learned spectra");
    match->audioThreadSync();
    check(match->computeCorrection(0.0f, 1.0f, 12.0f, -12.0f, false), "linear-phase correction builds");
    check(match->getLatencySamples() == Match::CONV_HOP, "queued linear IR cannot change active minimum-phase latency");
    match->audioThreadSync();
    check(match->getLatencySamples() == Match::CONV_HOP + Match::FIR_LENGTH / 2, "linear latency is adopted with its IR");
    check(match->computeCorrection(0.0f, 1.0f, 12.0f, -12.0f, true), "minimum-phase correction builds");
    check(match->getLatencySamples() == Match::CONV_HOP + Match::FIR_LENGTH / 2,
          "queued minimum-phase IR cannot change active linear latency");
    match->audioThreadSync();
    check(match->getLatencySamples() == Match::CONV_HOP, "minimum-phase latency is adopted with its IR");
}
}

int main()
{
    blockOwnership(false); blockOwnership(true); concurrentReplacement(); phaseMetadata();
    std::printf("Multi-Q Match IR: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: exercise actual host/UI state and lifecycle paths, beyond the Match core.
#include "../MultiQPlugin.cpp"
#include "src/DafPluginInternal.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace
{
using namespace DAF_NAMESPACE;
using Match = duskaudio::MultiQMatch;
int checks = 0, failures = 0;

void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

std::string state(PluginExporter& host) { return host.getStateValue("matchData").buffer(); }

void blocks(PluginExporter& host, int count = 8, bool reference = false, bool mono = false)
{
    std::array<float, 256> input{}, left{}, right{};
    uint32_t random = 0x83141u;
    float filtered = 0.0f;
    for (int b = 0; b < count; ++b)
    {
        for (float& sample : input)
        {
            random = random * 1664525u + 1013904223u;
            const float noise = 0.2f * (float(random >> 8) / 8388608.0f - 1.0f);
            filtered += 0.1f * (noise - filtered);
            sample = reference ? filtered : noise;
        }
        const float* inputs[] = {input.data(), mono ? nullptr : input.data()};
        float* outputs[] = {left.data(), mono ? nullptr : right.data()};
        host.run(inputs, outputs, uint32_t(input.size()));
    }
}

std::string learn(PluginExporter& host)
{
    host.setParameterValue(kParamEqType, float(duskaudio::EQType::Match));
    void* p = host.getInstancePointer();
    multiQMatchStartLearnCurrent(p, true); blocks(host, 64);
    multiQMatchStartLearnReference(p, true); blocks(host, 64, true);
    multiQMatchStartLearnReference(p, false); blocks(host);
    check(multiQMatchHasCurrent(p) && multiQMatchHasReference(p), "real shell learns both spectra");
    check(multiQMatchCompute(p), "real UI bridge computes a correction"); blocks(host);
    check(multiQMatchHasCorrection(p) && host.getLatency() == Match::CONV_HOP,
          "real Match audio path adopts its FIR and latency");
    return state(host);
}

void restores(const std::string& valid)
{
    {
        PluginExporter host(nullptr, nullptr, nullptr, nullptr);
        check(host.validateStateValue("matchData", valid.c_str())
                  && host.validateStateValue("matchData", "")
                  && !host.validateStateValue("matchData", "invalid Match data"),
              "wrapper preflight rejects malformed Match data before applying host parameters");
        host.setState("matchData", valid.c_str());
        check(state(host) == valid, "state restored before activation can be saved immediately");
        host.setState("matchData", "invalid Match data");
        check(state(host) == valid, "pre-activation rejection preserves the accepted state");
        host.activate(); blocks(host);
        check(state(host) == valid && multiQMatchHasCorrection(host.getInstancePointer()),
              "accepted pre-activation state survives a later rejected load");
        host.deactivate();
    }
    {
        PluginExporter host(nullptr, nullptr, nullptr, nullptr);
        host.activate(); host.setState("matchData", valid.c_str()); blocks(host);
        host.setState("matchData", "invalid Match data");
        check(state(host) == valid, "live rejection preserves current state");
        host.deactivate();
        check(state(host) == valid, "deactivation retains the last live state for host saves");
        host.activate(); blocks(host);
        check(state(host) == valid && multiQMatchHasCorrection(host.getInstancePointer()),
              "rejected live state cannot poison a later restart");
        host.deactivate();
    }
}

void inactiveUi(const std::string& valid)
{
    PluginExporter host(nullptr, nullptr, nullptr, nullptr);
    host.activate(); host.setParameterValue(kParamEqType, float(duskaudio::EQType::Match));
    host.setState("matchData", valid.c_str()); blocks(host); host.deactivate();
    void* p = host.getInstancePointer();
    check(multiQMatchHasCurrent(p) && multiQMatchHasReference(p) && multiQMatchHasCorrection(p),
          "deactivation retains learned spectra for the still-open editor");
    host.setParameterValue(kParamMatchApply, 50.0f);
    check(multiQMatchCompute(p), "the editor can recompute an inactive prepared instance");
    const std::string edited = state(host);
    check(edited != valid, "inactive UI computation makes a real state change");
    host.activate(); blocks(host);
    check(state(host) == edited, "reactivation preserves a correction edited while inactive");
    host.deactivate(); multiQMatchClear(p); host.activate(); blocks(host);
    check(!multiQMatchHasCurrent(p) && !multiQMatchHasCorrection(p),
          "Clear queued while inactive is adopted before the next prepare");
    host.deactivate();
}

void monoAndBypass(const std::string& valid)
{
    PluginExporter host(nullptr, nullptr, nullptr, nullptr);
    host.setAudioPortIO(1, 1); host.activate();
    host.setParameterValue(kParamEqType, float(duskaudio::EQType::Match));
    host.setState("matchData", valid.c_str()); blocks(host, 8, false, true);
    host.deactivate(); host.activate(); blocks(host, 8, false, true);
    check(state(host) == valid && host.getLatency() == Match::CONV_HOP,
          "mono restart preserves learned state with no second channel buffer");
    host.setParameterValue(kParamBypass, 1.0f);
    host.setState("matchData", ""); blocks(host, 8, false, true);
    host.deactivate(); host.activate();
    host.setParameterValue(kParamBypass, 0.0f); blocks(host, 8, false, true);
    check(!multiQMatchHasCorrection(host.getInstancePointer()) && host.getLatency() == 0,
          "empty state queued under bypass remains cleared after restart");
    host.setParameterValue(kParamBypass, 1.0f);
    host.setState("matchData", valid.c_str());
    host.deactivate(); host.activate(); host.setParameterValue(kParamBypass, 0.0f);
    blocks(host, 8, false, true);
    check(state(host) == valid && host.getLatency() == Match::CONV_HOP,
          "populated state queued under bypass remains active after restart");
    host.deactivate();
}

void concurrentLifecycle(const std::string& valid)
{
    PluginExporter host(nullptr, nullptr, nullptr, nullptr);
    host.activate(); host.setState("matchData", valid.c_str()); blocks(host);
    std::atomic<bool> ready{false}, done{false};
    std::atomic<int> reads{0}, computed{0}, invalid{0};
    std::thread ui([&] {
        std::array<float, Match::NUM_BINS> curve{};
        ready.store(true, std::memory_order_release);
        do
        {
            if (multiQMatchCompute(host.getInstancePointer())) ++computed;
            multiQMatchGetCurrentDb(host.getInstancePointer(), curve.data(), int(curve.size()));
            multiQMatchGetReferenceDb(host.getInstancePointer(), curve.data(), int(curve.size()));
            multiQMatchGetCorrectionDb(host.getInstancePointer(), curve.data(), int(curve.size()));
            const std::string saved = state(host);
            if (!Match::validateState(saved)) ++invalid;
            ++reads;
            std::this_thread::yield();
        } while (!done.load(std::memory_order_acquire));
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = 0; i < 12; ++i)
    {
        const int before = reads.load(std::memory_order_acquire);
        host.setSampleRate(i & 1 ? 44100.0 : 96000.0, true);
        host.setBufferSize(i & 1 ? 128 : 512, true);
        // Require UI work between host changes, rather than accepting one read
        // scheduled only after all reprepares had already finished.
        while (reads.load(std::memory_order_acquire) <= before) std::this_thread::yield();
    }
    done.store(true, std::memory_order_release); ui.join();
    check(reads >= 12 && computed >= 12 && invalid == 0,
          "UI compute/read/save remains valid during quiescent host reprepares");
    host.setState("matchData", valid.c_str()); blocks(host);
    check(state(host) == valid, "host restore remains usable after lifecycle contention");
    host.deactivate();
    std::printf("Host lifecycle contention: %d UI reads, %d corrections, %d invalid states\n",
                reads.load(), computed.load(), invalid.load());
}

void lifecycle()
{
    PluginExporter host(nullptr, nullptr, nullptr, nullptr);
    host.activate();
    const std::string empty = state(host), learned = learn(host);
    check(learned != empty, "learned state differs from the actual empty baseline");
    host.deactivate(); host.activate(); blocks(host);
    check(state(host) == learned && multiQMatchHasCorrection(host.getInstancePointer()),
          "UI-learned correction survives deactivate/activate without a host state load");
    host.setState("matchData", learned.c_str()); blocks(host);
    host.setSampleRate(96000.0, true); blocks(host);
    check(state(host) == learned, "sample-rate reprepare preserves complete live Match state");
    host.setBufferSize(512, true); blocks(host);
    check(state(host) == learned, "buffer-size reprepare preserves complete live Match state");
    host.setSampleRate(96000.0, true); host.setBufferSize(512, true); blocks(host);
    check(state(host) == learned, "identical repeated lifecycle changes preserve state");
    multiQMatchClear(host.getInstancePointer()); blocks(host);
    const std::string cleared = state(host);
    check(!multiQMatchHasCurrent(host.getInstancePointer()) && !multiQMatchHasCorrection(host.getInstancePointer()),
          "UI Clear actually removes learned data and correction");
    host.deactivate(); host.activate(); blocks(host);
    check(state(host) == cleared && !multiQMatchHasCorrection(host.getInstancePointer()),
          "an old host restore cannot revive a later UI Clear on restart");

    host.setState("matchData", learned.c_str()); blocks(host);
    host.setState("matchData", ""); blocks(host);
    check(state(host) == empty && !multiQMatchHasCorrection(host.getInstancePointer()) && host.getLatency() == 0,
          "default empty host state clears an active correction and latency");
    host.deactivate();
    restores(learned);
    inactiveUi(learned);
    monoAndBypass(learned);
    concurrentLifecycle(learned);
}

void presetOwnership()
{
    PluginExporter host(nullptr, nullptr, nullptr, nullptr);
    host.setState("uiPresets", "1,2,3");
    const auto held = multiQGetUiPresets(host.getInstancePointer());
    host.setState("uiPresets", "7,8,9");
    check(std::string(held) == "1,2,3", "preset bridge returns an owned view across later host writes");
    std::atomic<bool> ready{false}, done{false};
    std::atomic<int> reads{0}, torn{0};
    std::thread ui([&] {
        ready.store(true, std::memory_order_release);
        do
        {
            const std::string snapshot = multiQGetUiPresets(host.getInstancePointer());
            if (snapshot != "1,2,3" && snapshot != "7,8,9") ++torn;
            ++reads;
        } while (!done.load(std::memory_order_acquire));
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = 0; i < 1000; ++i) host.setState("uiPresets", i & 1 ? "1,2,3" : "7,8,9");
    done.store(true, std::memory_order_release); ui.join();
    check(reads > 0 && torn == 0, "UI preset reads and host restores remain complete under contention");
}
}

int main()
{
    d_nextBufferSize = 256;
    d_nextSampleRate = 48000.0;
    lifecycle(); presetOwnership();
    std::printf("Multi-Q host state: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

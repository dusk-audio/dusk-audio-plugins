// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: exercise actual host/UI state and lifecycle paths, beyond the Match core.
#include "../MultiQPlugin.cpp"
#include "src/DafPluginInternal.hpp"
#include "DuskDenormals.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
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

uint64_t fpControl()
{
#if DUSK_HAS_SSE
    return _mm_getcsr();
#elif defined(__aarch64__)
    uint64_t value; asm volatile("mrs %0, fpcr" : "=r"(value)); return value;
#else
    return 0;
#endif
}
void setFpControl(uint64_t value)
{
#if DUSK_HAS_SSE
    _mm_setcsr(static_cast<unsigned>(value));
#elif defined(__aarch64__)
    asm volatile("msr fpcr, %0" :: "r"(value));
#else
    (void)value;
#endif
}

void denormals()
{
#if DUSK_HAS_SSE || defined(__aarch64__)
    const uint64_t original = fpControl();
#if DUSK_HAS_SSE
    constexpr uint64_t flushMask = 0x8040;
    const uint64_t cleanControl = original & ~uint64_t{0x3f}; // clear exception-status flags
#else
    constexpr uint64_t flushMask = uint64_t{1} << 24;
    const uint64_t cleanControl = original;
#endif
    PluginExporter host(nullptr, nullptr, nullptr, nullptr); host.activate();
    host.setParameterValue(kParamMasterGain, -6.0f);
    std::array<float, 256> input{}, left{}, right{};
    const float* inputs[] = {input.data(), input.data()};
    float* outputs[] = {left.data(), right.data()};
    input.fill(0.25f); host.run(inputs, outputs, uint32_t(input.size()));
    check(std::abs(left.back() - 0.25f * std::pow(10.0f, -6.0f / 20.0f)) < 1e-6f,
          "denormal probe exercises real master-gain arithmetic");
    const uint32_t tinyBits = 0x00400000; // a nonzero subnormal, copied without FP arithmetic
    float tiny; std::memcpy(&tiny, &tinyBits, sizeof(tiny));
    for (bool callerFlush : {false, true})
    {
        input.fill(tiny);
        setFpControl(callerFlush ? cleanControl | flushMask : cleanControl & ~flushMask);
        const uint64_t before = fpControl();
        host.run(inputs, outputs, uint32_t(input.size()));
        const uint64_t after = fpControl();
        bool zero = true;
        for (size_t i = 0; i < input.size(); ++i)
        {
            uint32_t l, r; std::memcpy(&l, &left[i], sizeof(l)); std::memcpy(&r, &right[i], sizeof(r));
            zero &= (l & 0x7fffffff) == 0 && (r & 0x7fffffff) == 0;
        }
        check(zero, "callback flushes subnormal arithmetic regardless of host FP settings");
        check(after == before, "callback restores the caller's complete FP control state");
        uint32_t outputBits; std::memcpy(&outputBits, &left.back(), sizeof(outputBits));
        std::printf("Denormal control: callerFlush=%d, outputBits=0x%08x, FP restored=%d\n",
                    callerFlush, outputBits, after == before);
    }
    host.setParameterValue(kParamBypass, 1.0f);
    const uint64_t before = fpControl();
    host.run(inputs, outputs, uint32_t(input.size()));
    check(std::memcmp(input.data(), left.data(), sizeof(input)) == 0
              && std::memcmp(input.data(), right.data(), sizeof(input)) == 0,
          "hard bypass retains bit-exact subnormal payloads");
    check(fpControl() == before, "bypass also restores caller FP control");
    host.deactivate(); setFpControl(original);
#else
    std::fprintf(stderr, "Unsupported architecture for the required denormal-control probe\n");
    check(false, "denormal test requires SSE or arm64 FP controls");
#endif
}

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

void zeroFrames(const std::string& valid)
{
    for (bool mono : {false, true})
    {
        PluginExporter host(nullptr, nullptr, nullptr, nullptr);
        host.setAudioPortIO(mono ? 1 : 2, mono ? 1 : 2); host.activate();
        host.setParameterValue(kParamEqType, float(duskaudio::EQType::Match));
        host.setState("matchData", valid.c_str()); blocks(host, 8, false, mono);
        check(host.getLatency() == Match::CONV_HOP, "zero-frame fixture has active Match latency");
        std::array<float, 1> input{{0.25f}}, output{{-0.75f}};
        const float* inputs[] = {input.data(), mono ? nullptr : input.data()};
        float* outputs[] = {output.data(), mono ? nullptr : output.data()};
        for (bool bypass : {false, true})
        {
            host.setParameterValue(kParamBypass, bypass ? 1.0f : 0.0f);
            const uint32_t latency = host.getLatency();
            const std::string before = state(host);
            host.run(inputs, outputs, 0);
            check(output[0] == -0.75f && host.getLatency() == latency && state(host) == before,
                  "empty callback leaves buffers, state and latency unchanged");
            host.run(nullptr, nullptr, 0);
            check(host.getLatency() == latency && state(host) == before,
                  "empty callback accepts absent channel arrays, including bypass");
        }
        host.deactivate();
    }
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
    zeroFrames(learned);
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

int main(int argc, char** argv)
{
    d_nextBufferSize = 256;
    d_nextSampleRate = 48000.0;
    if (argc == 2 && std::strcmp(argv[1], "--denormals") == 0) denormals();
    else if (argc == 2 && std::strcmp(argv[1], "--zero") == 0)
    {
        PluginExporter host(nullptr, nullptr, nullptr, nullptr); host.activate();
        const std::string learned = learn(host); host.deactivate(); zeroFrames(learned);
    }
    else { lifecycle(); presetOwnership(); denormals(); }
    std::printf("Multi-Q host state: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

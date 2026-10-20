// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #292: the audio thread owns the effective motor rate. A late synced block
// must not strand the DSP at its synced rate after a host turns sync off.
#include "../TapeEchoPlugin.cpp"
#include "src/DafPluginInternal.hpp"

#include <array>
#include <cstdio>
#include <thread>

namespace duskaudio
{
struct TapeEchoDSPTestAccess
{
    static float rate(const TapeEchoDSP& dsp)
    {
        return dsp.pRepeatRate.load(std::memory_order_relaxed);
    }
};
}

START_NAMESPACE_DAF
struct TapeEchoPluginTestAccess
{
    static duskaudio::TapeEchoDSP& dsp(PluginExporter& host)
    {
        return static_cast<TapeEchoPlugin*>(host.getInstancePointer())->dsp;
    }
    static void mono(PluginExporter& host, bool enabled)
    {
        static_cast<TapeEchoPlugin*>(host.getInstancePointer())->ioChanged(
            enabled ? 1 : 2, enabled ? 1 : 2);
    }
};
END_NAMESPACE_DAF

namespace
{
using namespace DAF_NAMESPACE;
using DSP = duskaudio::TapeEchoDSP;
int checks = 0, failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

float rate(PluginExporter& host)
{
    return duskaudio::TapeEchoDSPTestAccess::rate(TapeEchoPluginTestAccess::dsp(host));
}

void block(PluginExporter& host, bool mono = false, uint32_t frames = 64)
{
    std::array<float, 64> input{}, left{}, right{};
    const float* inputs[] = {input.data(), mono ? nullptr : input.data()};
    float* outputs[] = {left.data(), mono ? nullptr : right.data()};
    host.run(inputs, outputs, frames);
}

void deterministicHandoff(PluginExporter& host)
{
    // Recreate the exact bad end state without depending on scheduler timing:
    // the host restores sync=off and the knob, then the in-flight synced block
    // commits its older rate. The next block must recover without another edit.
    for (bool mono : {false, true})
        for (bool bypass : {false, true})
        {
            host.deactivate();
            TapeEchoPluginTestAccess::mono(host, mono);
            host.activate();
            host.setParameterValue(kParamBypass, bypass ? 1.0f : 0.0f);
            host.setParameterValue(kParamTempoSync, 1.0f);
            block(host, mono);
            const float synced = rate(host);
            const float knob = synced > 0.5f ? 0.13f : 0.87f;
            host.setParameterValue(kParamRepeatRate, knob);
            host.setParameterValue(kParamTempoSync, 0.0f);
            TapeEchoPluginTestAccess::dsp(host).setRepeatRate(synced);
            for (int i = 0; i < 4; ++i)
            {
                block(host, mono);
                check(rate(host) == knob, "sync off recovers from a late synced write");
            }
            check(std::abs(tapeEchoGetHead1DelayMs(host.getInstancePointer())
                           - DSP::delayMsForRepeatRate(knob)) < 0.001f,
                  "UI delay agrees with the actual DSP motor rate");
        }
    host.deactivate();
    TapeEchoPluginTestAccess::mono(host, false);
    host.activate();
    host.setParameterValue(kParamBypass, 0.0f);
}

void ownerAndNeighbours(PluginExporter& host)
{
    host.setParameterValue(kParamTempoSync, 0.0f);
    block(host);
    const float previous = rate(host);
    const float knob = previous > 0.5f ? 0.2f : 0.8f;
    host.setParameterValue(kParamRepeatRate, knob);
    check(rate(host) == previous, "a host knob write does not write the DSP motor rate");
    block(host, false, 0);
    check(rate(host) == previous, "an empty block does not advance the motor control");
    block(host);
    check(rate(host) == knob, "a nonempty block applies the manual knob");

    TimePosition tp;
    tp.bbt.valid = true;
    tp.bbt.beatsPerMinute = 120.0;
    host.setTimePosition(tp);
    host.setParameterValue(kParamMode, 1.0f);
    host.setParameterValue(kParamTempoSync, 1.0f);
    host.setParameterValue(kParamSyncDivision, 2.0f); // legacy 1/16 = 125 ms
    block(host);
    check(std::abs(DSP::delayMsForRepeatRate(rate(host)) - 125.0f) < 0.001f,
          "legacy sync division still selects its exact delay");
    const float synced = rate(host);
    host.setParameterValue(kParamRepeatRate, knob == 0.8f ? 0.1f : 0.9f);
    block(host);
    check(rate(host) == synced, "the manual knob cannot override active sync");
    host.setParameterValue(kParamEchoRateNote, 5.0f);
    block(host);
    const int division = teDivisionForSyncKnobPos(4, teLeadingHeadIndexForMode(1));
    const float expected = std::clamp(static_cast<float>(syncDelayMs(120.0, division)),
                                    DSP::kMinDelayMs, DSP::kMaxDelayMs);
    check(std::abs(DSP::delayMsForRepeatRate(rate(host)) - expected) < 0.001f,
          "the physical note takes ownership from the legacy division");
    const float beforePrepare = rate(host);
    host.deactivate(); host.activate();
    check(rate(host) == beforePrepare, "sync reactivation preserves the effective motor rate");
    host.setSampleRate(88200.0, true);
    check(rate(host) == beforePrepare, "sync rate changes do not seed the inactive manual knob");

    host.setParameterValue(kParamTempoSync, 0.0f);
    host.setParameterValue(kParamRepeatRate, 0.61f);
    host.deactivate(); host.activate();
    check(rate(host) == 0.61f, "activation seeds the restored manual rate before preparing");
    host.setSampleRate(96000.0, true);
    host.setSampleRate(96000.0, true);
    block(host);
    check(rate(host) == 0.61f, "repeat preparation preserves the manual rate");
}

void stress(PluginExporter& host)
{
    std::atomic<bool> start{false}, done{false};
    std::thread loader([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < 2000; ++i)
        {
            host.loadProgram(static_cast<uint32_t>(i % kNumFactoryPresets));
            host.setParameterValue(kParamRepeatRate, (i % 101) * 0.01f);
            host.setParameterValue(kParamTempoSync, static_cast<float>(i % 2));
        }
        done.store(true, std::memory_order_release);
    });
    start.store(true, std::memory_order_release);
    int blocks = 0;
    do { block(host); ++blocks; } while (!done.load(std::memory_order_acquire) || blocks < 200);
    loader.join();
    host.setParameterValue(kParamRepeatRate, 0.37f);
    host.setParameterValue(kParamTempoSync, 0.0f);
    block(host);
    check(rate(host) == 0.37f, "concurrent programs and automation converge when sync turns off");
    std::printf("Concurrent program/parameter stress: %d audio blocks\n", blocks);
}
}

int main()
{
    d_nextBufferSize = 64;
    d_nextSampleRate = 48000.0;
    PluginExporter host(nullptr, nullptr, nullptr, nullptr);
    host.activate();
    deterministicHandoff(host);
    ownerAndNeighbours(host);
    stress(host);
    host.deactivate();
    std::printf("Tape Echo rate handoff: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

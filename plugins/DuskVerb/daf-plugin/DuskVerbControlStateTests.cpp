// Copyright (C) 2026 Dusk Audio — GPL-3.0-or-later.
#include "DuskVerbControlState.hpp"
#include <cstdio>
#include <thread>
#include <atomic>

int main()
{
    using namespace duskverb;
    ControlState controls;
    int failures = 0;
    auto check = [&](bool okay, const char* message) {
        if (!okay) { ++failures; std::printf("FAIL %s\n", message); }
    };
    StateValues original = controls.snapshot();
    original.params[Mix] = 0.25f;
    original.params[Size] = 0.75f;
    original.params[Bypass] = 1.0f;
    original.sixAP.densityBaseline = 0.2f;
    original.sixAP.outputTrim = 0.8f;
    original.userName = "Complete sound";
    controls.publish(original);
    auto read = controls.snapshot();
    check(read.params == original.params && read.userName == original.userName
       && read.sixAP.densityBaseline == 0.2f && read.sixAP.outputTrim == 0.8f,
          "complete user sound including hidden config and identity");
    controls.setParameter(Mix, 0.7f);
    check(controls.parameter(Mix) == 0.7f && controls.snapshot().edited,
          "automation following recall wins and marks edited");
    controls.publish(original);
    check(controls.parameter(Mix) == 0.25f && !controls.snapshot().edited,
          "recall supersedes earlier automation and clears edited");
    for (int i = 0; i < kNumParams; ++i) controls.setParameter(i, controls.parameter(i));
    check(!controls.snapshot().edited, "host echo of unchanged values preserves recalled identity");
    const auto beforeRename = controls.sound();
    auto renamed = controls.snapshot();
    renamed.userName = "Renamed without changing sound";
    controls.publish(renamed);
    check(controls.sound().recallEpoch == beforeRename.recallEpoch,
          "saving or renaming an unchanged sound does not restart its tail");
    renamed.sixAP.outputTrim = 0.9f;
    controls.publish(renamed);
    check(controls.sound().recallEpoch != beforeRename.recallEpoch,
          "changing hidden sound configuration does request a recall");

    for (uint32_t program = 0; program < duskverb::dv2FactoryPresets().size(); ++program)
    {
        controls.publish(original);
        controls.setParameter(Tone, -0.7f);
        auto expected = controls.snapshot();
        applyFactoryPresetToHostParameters(duskverb::dv2FactoryPresets()[program], [&](int i, float v) {
            expected.params[i] = v;
        });
        controls.loadProgram(program);
        const auto factory = controls.snapshot();
        check(factory.params == expected.params, "factory writes its complete set and preserves neighbors");
        check(factory.params[Bypass] == 1.0f && factory.params[Tone] == -0.7f,
              "factory preserves bypass and macro edits");
        check(factory.userName.empty() && !factory.edited
           && factory.presetName == duskverb::dv2FactoryPresets()[program].name,
              "host factory recall replaces user identity and edited status");
        controls.setParameter(Mix, 0.4f);
        check(controls.parameter(Mix) == 0.4f, "automation follows factory recall");
    }

    // MIX LOCK on the host's program path: a locked recall keeps the Mix and Bus
    // in effect, even ones an earlier program set (they are in no published
    // state); it stays an un-edited recall; unlocking moves nothing; and a send
    // keeps its Bus through any number of recalls.
    {
        const auto& presets = duskverb::dv2FactoryPresets();
        const auto factoryValue = [&](uint32_t program, int param) {
            float value = -1.0f;
            applyFactoryPresetToHostParameters(presets[program], [&](int i, float v) { if (i == param) value = v; });
            return value;
        };
        controls.publish(original);
        controls.setMixLock(true);
        controls.loadProgram(0);
        check(controls.parameter(Mix) == 0.25f, "a locked recall keeps the published Mix");
        check(!controls.snapshot().edited && controls.snapshot().presetName == presets[0].name,
              "a locked recall is a clean recall of that program");
        controls.setMixLock(false);
        controls.loadProgram(1);
        check(controls.parameter(Mix) == factoryValue(1, Mix), "unlocked, a recall writes its program's Mix");
        controls.setMixLock(true);
        controls.loadProgram(2);
        check(controls.parameter(Mix) == factoryValue(1, Mix),
              "a locked recall keeps a Mix an earlier program set");
        controls.setMixLock(false);
        check(controls.parameter(Mix) == factoryValue(1, Mix), "unlocking does not move the sound");
        controls.setMixLock(true);
        controls.setParameter(BusMode, 1.0f);
        for (uint32_t program = 3; program < 6; ++program)
            controls.loadProgram(program);
        check(controls.parameter(BusMode) == 1.0f && factoryValue(5, BusMode) == 0.0f,
              "a send keeps Bus on through locked recalls of programs that turn it off");
        controls.setMixLock(false);
    }

    // Every snapshot is one of two deliberately distinct complete states.
    // Reading an in-progress parameter loop would produce a forbidden pair.
    auto a = original, b = original;
    b.params[Mix] = 0.75f;
    b.params[Size] = 0.25f;
    b.sixAP.outputTrim = 1.2f;
    controls.publish(a);
    std::atomic<bool> done{false};
    std::atomic<int> torn{0};
    std::atomic<unsigned> reads{0};
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire))
        {
            const auto sound = controls.sound();
            const bool isA = sound.params[Mix] == 0.25f && sound.params[Size] == 0.75f
                          && sound.sixAP.outputTrim == 0.8f;
            const bool isB = sound.params[Mix] == 0.75f && sound.params[Size] == 0.25f
                          && sound.sixAP.outputTrim == 1.2f;
            if (!isA && !isB) torn.fetch_add(1, std::memory_order_relaxed);
            reads.fetch_add(1, std::memory_order_release);
        }
    });
    while (reads.load(std::memory_order_acquire) == 0) std::this_thread::yield();
    for (int i = 0; i < 20000; ++i) controls.publish((i & 1) ? a : b);
    done.store(true, std::memory_order_release);
    reader.join();
    check(torn.load() == 0, "concurrent recall never exposes partial parameters/configuration");
    check(reads.load() > 0, "concurrent reader actually sampled the publication path");
    std::printf("Control state: %d failures; %zu factory programs; 20000 concurrent recalls\n", failures,
                duskverb::dv2FactoryPresets().size());
    return failures ? 1 : 0;
}

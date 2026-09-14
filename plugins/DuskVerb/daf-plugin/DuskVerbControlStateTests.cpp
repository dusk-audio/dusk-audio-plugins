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

    for (uint32_t program = 0; program < getFactoryPresets().size(); ++program)
    {
        controls.publish(original);
        controls.setParameter(Tone, -0.7f);
        auto expected = controls.snapshot();
        applyFactoryPresetToHostParameters(getFactoryPresets()[program], [&](int i, float v) {
            expected.params[i] = v;
        });
        controls.loadProgram(program);
        const auto factory = controls.snapshot();
        check(factory.params == expected.params, "factory writes its complete set and preserves neighbors");
        check(factory.params[Bypass] == 1.0f && factory.params[Tone] == -0.7f,
              "factory preserves bypass and macro edits");
        check(factory.userName.empty() && !factory.edited
           && factory.presetName == getFactoryPresets()[program].name,
              "host factory recall replaces user identity and edited status");
        controls.setParameter(Mix, 0.4f);
        check(controls.parameter(Mix) == 0.4f, "automation follows factory recall");
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
    std::printf("Control state: %d failures; 20 factory programs; 20000 concurrent recalls\n", failures);
    return failures ? 1 : 0;
}

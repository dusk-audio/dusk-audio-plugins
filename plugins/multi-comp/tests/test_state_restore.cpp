/*
 * State-restore integrity gate.
 *
 * pluginval level 10 intermittently reports a bool parameter not restored after
 * setStateInformation ("Limit Mode" on the shipped v1.3.2, "Over Easy" on a dev
 * build — different params, both with a leftover value of ~0.385 where 0 was
 * expected). This reproduces the validator's sequence deterministically and
 * many times over, on the message thread and from a background thread, so the
 * failure stops being a coin flip and names the parameter and value instead.
 *
 * Sequence per iteration (mirrors pluginval's state restoration test):
 *   1. randomize every parameter via setValueNotifyingHost
 *   2. snapshot getValue() of all parameters + getStateInformation
 *   3. randomize everything again (so restore has real work to do)
 *   4. setStateInformation (variant A: same thread, variant B: std::thread)
 *   5. compare every parameter's getValue() to the snapshot
 */

#include <JuceHeader.h>
#include "../UniversalCompressor.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <thread>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sr = 48000.0;
    constexpr int block = 512;
    constexpr int iterations = 200;
    constexpr float tolerance = 0.01f;   // pluginval uses 0.1; be stricter

    UniversalCompressor plugin;
    plugin.setPlayConfigDetails(2, 2, sr, block);
    plugin.prepareToPlay(sr, block);

    // UniversalCompressor::getParameters() shadows the AudioProcessor accessor
    // and returns the APVTS; the base-class one gives the host-visible list.
    const auto& paramList = static_cast<juce::AudioProcessor&>(plugin).getParameters();

    juce::Random rng(0x57A7E);

    struct Mismatch { int count = 0; float worst = 0.0f; float expected = 0.0f, actual = 0.0f; };
    std::map<juce::String, Mismatch> mismatches[2];   // [0]=same thread, [1]=background

    for (int iter = 0; iter < iterations; ++iter)
    {
        const int variant = iter % 2;                 // alternate same-thread / background

        // 1. randomize
        for (auto* p : paramList)
            p->setValueNotifyingHost(rng.nextFloat());

        // 2. snapshot
        std::vector<float> expected;
        expected.reserve(static_cast<size_t>(paramList.size()));
        for (auto* p : paramList)
            expected.push_back(p->getValue());

        juce::MemoryBlock state;
        plugin.getStateInformation(state);

        // 3. scramble
        for (auto* p : paramList)
            p->setValueNotifyingHost(rng.nextFloat());

        // 4. restore
        if (variant == 0)
        {
            plugin.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        }
        else
        {
            std::thread restorer([&] {
                plugin.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            });
            restorer.join();
        }

        // 5. compare immediately — no message-loop pumping, matching a host that
        // reads back synchronously after restore.
        for (int i = 0; i < paramList.size(); ++i)
        {
            auto* p = paramList[static_cast<int>(i)];
            const float actual = p->getValue();
            const float diff = std::abs(actual - expected[static_cast<size_t>(i)]);
            if (diff > tolerance)
            {
                auto& m = mismatches[variant][p->getName(64)];
                ++m.count;
                if (diff > m.worst)
                {
                    m.worst = diff;
                    m.expected = expected[static_cast<size_t>(i)];
                    m.actual = actual;
                }
            }
        }
    }

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "\n--- State-restore integrity (" << iterations << " iterations, tolerance "
              << tolerance << ") ---\n";

    int totalFailures = 0;
    const char* variantNames[2] = { "same-thread restore", "background-thread restore" };
    for (int v = 0; v < 2; ++v)
    {
        std::cout << "\n  " << variantNames[v] << ":\n";
        if (mismatches[v].empty())
            std::cout << "    all parameters restored on every iteration\n";
        for (const auto& [name, m] : mismatches[v])
        {
            totalFailures += m.count;
            std::cout << "    MISMATCH " << std::setw(24) << std::left << name.toStdString()
                      << std::right << " x" << m.count
                      << "  worst: expected " << m.expected << " got " << m.actual << "\n";
        }
    }

    std::cout << "\n  RESULT: " << (totalFailures == 0 ? "PASS" : "FAIL")
              << " (" << totalFailures << " parameter mismatches)\n";
    return totalFailures == 0 ? 0 : 1;
}

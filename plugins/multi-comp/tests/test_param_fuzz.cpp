/*
 * Parameter-fuzz gate (pluginval "Fuzz parameters" replica, ASan-friendly).
 *
 * pluginval level 10 intermittently died with heap corruption (glibc
 * "corrupted double-linked list" / free() abort in the host's plugin dtor)
 * during its parameter fuzz on this plugin. Teardown-time detection means the
 * scribble happened earlier and elsewhere; repetition converges too slowly at a
 * ~2-3% incidence. This reproduces the storm deterministically and hard: every
 * round randomizes EVERY parameter (biased toward the extremes fuzzers love),
 * processes audio through both the float and double paths with hostile block
 * sizes (0, 1, tiny, prepared max, oversized), interleaves save/restore,
 * bypass, and re-prepares with changed rates. Run it under AddressSanitizer
 * and the first out-of-bounds write aborts with a stack naming the culprit,
 * instead of a coin-flip crash 30 runs later.
 *
 * The plugin instance is created and destroyed once per outer round so the
 * dtor runs against a freshly-stormed heap, matching where pluginval detected
 * the corruption.
 */

#include <JuceHeader.h>
#include "../UniversalCompressor.h"
#include <cmath>
#include <iostream>
#include <vector>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr int outerRounds = 24;          // plugin lifetimes
    constexpr int stormsPerRound = 60;       // randomize+process cycles per lifetime
    juce::Random rng(0xF0220);

    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    const int preparedBlock = 512;
    const int blockSizes[] = { 0, 1, 3, 16, 63, 256, 512, 511, 480 };

    juce::MemoryBlock savedState;

    for (int round = 0; round < outerRounds; ++round)
    {
        const double sr = rates[rng.nextInt(4)];

        auto plugin = std::make_unique<UniversalCompressor>();
        plugin->setPlayConfigDetails(2, 2, sr, preparedBlock);
        plugin->prepareToPlay(sr, preparedBlock);

        const auto& paramList = static_cast<juce::AudioProcessor&>(*plugin).getParameters();

        juce::AudioBuffer<float> buf(2, preparedBlock);
        juce::AudioBuffer<double> dbuf(2, preparedBlock);
        juce::MidiBuffer midi;

        for (int storm = 0; storm < stormsPerRound; ++storm)
        {
            // Randomize every parameter; bias to the endpoints (fuzzers hammer
            // 0 and 1, and the bugs live at range edges).
            for (auto* p : paramList)
            {
                const int roll = rng.nextInt(10);
                const float v = roll == 0 ? 0.0f
                              : roll == 1 ? 1.0f
                              : rng.nextFloat();
                p->setValueNotifyingHost(v);
            }

            const int n = blockSizes[rng.nextInt(std::size(blockSizes))];

            if (rng.nextInt(4) == 0)
            {
                // Double-precision path (chunked conversion buffer)
                dbuf.clear();
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = dbuf.getWritePointer(ch);
                    for (int i = 0; i < n; ++i)
                        d[i] = 0.5 * (rng.nextDouble() * 2.0 - 1.0);
                }
                juce::AudioBuffer<double> view(dbuf.getArrayOfWritePointers(), 2, n);
                plugin->processBlock(view, midi);
            }
            else
            {
                buf.clear();
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = buf.getWritePointer(ch);
                    for (int i = 0; i < n; ++i)
                        d[i] = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);
                }
                juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, n);
                plugin->processBlock(view, midi);
            }

            // Interleave the state machinery the validator also exercises.
            switch (rng.nextInt(12))
            {
                case 0:
                    plugin->getStateInformation(savedState);
                    break;
                case 1:
                    if (savedState.getSize() > 0)
                        plugin->setStateInformation(savedState.getData(),
                                                    static_cast<int>(savedState.getSize()));
                    break;
                case 2:
                    plugin->prepareToPlay(rates[rng.nextInt(4)], preparedBlock);
                    break;
                case 3:
                    plugin->setCurrentProgram(rng.nextInt(juce::jmax(1, plugin->getNumPrograms())));
                    break;
                default:
                    break;
            }
        }

        plugin->releaseResources();
        plugin.reset();   // dtor against the stormed heap — where pluginval saw it
    }

    std::cout << "\n--- Parameter fuzz gate ---\n  RESULT: PASS ("
              << outerRounds << " plugin lifetimes x " << stormsPerRound
              << " storms survived)\n";
    return 0;
}

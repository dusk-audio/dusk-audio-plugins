// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 0.6 — DuskAmp CPU harness.
//
// Instantiates DuskAmpProcessor, applies each factory preset, streams the
// shared synthetic DI through processBlock at 48 kHz / 256 samples, and
// records per-block wall-clock time. Reports p50, p95, p99, max in ms per
// preset, plus a worst-case roll-up.
//
// Output: Markdown to stdout (human-readable summary), JSON to a file
// (machine-readable for trend tracking and CI gates).
//
// Per-phase CPU gates are evaluated as:
//   baseline_p95 + Σ(per-phase deltas) ≤ target
// where baseline_p95 is the worst preset's p95 from the current build, and
// the target depends on the host (≤ 2.0 ms on M1, ≤ 3.5 ms on i5-8250U).

#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include "SyntheticDI.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr double kSampleRate    = 48000.0;
    constexpr int    kBlockSize     = 256;
    constexpr int    kDurationSec   = 30;
    constexpr int    kTotalSamples  = static_cast<int> (kSampleRate * kDurationSec);
    constexpr int    kWarmupBlocks  = 64;   // discard timings during cache/JIT warm-up

    struct Stats
    {
        double p50;
        double p95;
        double p99;
        double mean;
        double max;
        int    samples;
    };

    Stats computeStats (std::vector<double>& blockMs)
    {
        std::sort (blockMs.begin(), blockMs.end());
        const int n = static_cast<int> (blockMs.size());
        if (n == 0) return { 0, 0, 0, 0, 0, 0 };

        const auto idx = [&] (double pct) {
            const int i = static_cast<int> (std::round (pct * (n - 1)));
            return static_cast<std::size_t> (std::clamp (i, 0, n - 1));
        };

        double sum = 0.0;
        for (auto v : blockMs) sum += v;

        return Stats {
            blockMs[idx (0.50)],
            blockMs[idx (0.95)],
            blockMs[idx (0.99)],
            sum / n,
            blockMs.back(),
            n
        };
    }

    struct PresetResult
    {
        std::string name;
        std::string category;
        Stats       stats;
    };

    PresetResult measurePreset (DuskAmpProcessor& processor,
                                const DuskAmpPreset& preset,
                                const juce::AudioBuffer<float>& di)
    {
        processor.reset();
        preset.applyTo (processor.parameters);

        // Warm up: run blocks of zeros so DSP state and SmoothedValue ramps settle.
        {
            juce::AudioBuffer<float> warm (2, kBlockSize);
            warm.clear();
            juce::MidiBuffer midi;
            for (int n = 0; n < 16; ++n)
                processor.processBlock (warm, midi);
        }

        std::vector<double> blockMs;
        blockMs.reserve (kTotalSamples / kBlockSize);

        juce::MidiBuffer midi;
        juce::AudioBuffer<float> block (2, kBlockSize);

        int blocksMeasured = 0;
        for (int sampleStart = 0;
             sampleStart + kBlockSize <= kTotalSamples;
             sampleStart += kBlockSize)
        {
            // Mono DI duplicated to both channels.
            block.copyFrom (0, 0, di.getReadPointer (0, sampleStart), kBlockSize);
            block.copyFrom (1, 0, di.getReadPointer (0, sampleStart), kBlockSize);

            const auto t0 = std::chrono::high_resolution_clock::now();
            processor.processBlock (block, midi);
            const auto t1 = std::chrono::high_resolution_clock::now();

            if (blocksMeasured >= kWarmupBlocks)
            {
                const std::chrono::duration<double, std::milli> dt = t1 - t0;
                blockMs.push_back (dt.count());
            }
            ++blocksMeasured;
        }

        return PresetResult {
            preset.name,
            preset.category,
            computeStats (blockMs)
        };
    }

    std::string padRight (const std::string& s, std::size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    std::string fmt (double v)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision (3) << v;
        return oss.str();
    }

    void writeMarkdown (std::ostream& os,
                        const std::vector<PresetResult>& results,
                        const std::string& platform)
    {
        os << "# DuskAmp CPU baseline\n\n";
        os << "Measured on **" << platform << "** at "
           << static_cast<int> (kSampleRate) << " Hz / "
           << kBlockSize << " samples / stereo. "
           << "Each preset processes a 30 s synthetic DI; first "
           << kWarmupBlocks << " blocks discarded as warm-up.\n\n";

        os << "Real-time budget at this block size: "
           << std::fixed << std::setprecision (3)
           << (1000.0 * kBlockSize / kSampleRate)
           << " ms per block.\n\n";

        os << "Per-phase CPU gates use the worst preset's p95.\n\n";

        os << "| # | Preset                  | Category | p50 ms | p95 ms | p99 ms | max ms |\n";
        os << "|---|-------------------------|----------|--------|--------|--------|--------|\n";

        double worstP95 = 0.0;
        std::string worstName;

        for (std::size_t i = 0; i < results.size(); ++i)
        {
            const auto& r = results[i];
            os << "| " << std::setw (2) << i << " | "
               << padRight (r.name, 23) << " | "
               << padRight (r.category, 8) << " | "
               << padRight (fmt (r.stats.p50), 6) << " | "
               << padRight (fmt (r.stats.p95), 6) << " | "
               << padRight (fmt (r.stats.p99), 6) << " | "
               << padRight (fmt (r.stats.max), 6) << " |\n";

            if (r.stats.p95 > worstP95)
            {
                worstP95 = r.stats.p95;
                worstName = r.name;
            }
        }

        os << "\n**Worst preset (p95): " << worstName << " at "
           << fmt (worstP95) << " ms.**\n";
    }

    void writeJson (std::ostream& os,
                    const std::vector<PresetResult>& results,
                    const std::string& platform)
    {
        os << "{\n";
        os << "  \"platform\": \"" << platform << "\",\n";
        os << "  \"sample_rate\": " << static_cast<int> (kSampleRate) << ",\n";
        os << "  \"block_size\": " << kBlockSize << ",\n";
        os << "  \"warmup_blocks\": " << kWarmupBlocks << ",\n";
        os << "  \"presets\": [\n";

        for (std::size_t i = 0; i < results.size(); ++i)
        {
            const auto& r = results[i];
            os << "    {\n";
            os << "      \"name\": \"" << r.name << "\",\n";
            os << "      \"category\": \"" << r.category << "\",\n";
            os << "      \"samples\": " << r.stats.samples << ",\n";
            os << "      \"p50_ms\": " << fmt (r.stats.p50) << ",\n";
            os << "      \"p95_ms\": " << fmt (r.stats.p95) << ",\n";
            os << "      \"p99_ms\": " << fmt (r.stats.p99) << ",\n";
            os << "      \"mean_ms\": " << fmt (r.stats.mean) << ",\n";
            os << "      \"max_ms\": " << fmt (r.stats.max) << "\n";
            os << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
        }

        os << "  ]\n";
        os << "}\n";
    }

    std::string detectPlatform()
    {
#if defined(__APPLE__) && defined(__aarch64__)
        return "macOS / Apple Silicon";
#elif defined(__APPLE__) && defined(__x86_64__)
        return "macOS / Intel";
#elif defined(__linux__) && defined(__x86_64__)
        return "Linux / x86_64";
#elif defined(_WIN32)
        return "Windows";
#else
        return "Unknown";
#endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;

    juce::File jsonOut;
    juce::File mdOut;
    if (argc > 1) jsonOut = juce::File (juce::String::fromUTF8 (argv[1]));
    if (argc > 2) mdOut   = juce::File (juce::String::fromUTF8 (argv[2]));

    const std::string platform = detectPlatform();

    std::cerr << "Synthesising DI..." << std::endl;
    const auto di = DuskAmpTest::makeSyntheticDI (kSampleRate, kDurationSec);

    DuskAmpProcessor processor;
    processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    std::vector<PresetResult> results;
    results.reserve (kNumFactoryPresets);

    for (int p = 0; p < kNumFactoryPresets; ++p)
    {
        const auto& preset = kFactoryPresets[p];
        std::cerr << "[" << (p + 1) << "/" << kNumFactoryPresets << "] " << preset.name << "..." << std::flush;
        results.push_back (measurePreset (processor, preset, di));
        const auto& s = results.back().stats;
        std::cerr << "  p50=" << fmt (s.p50) << " p95=" << fmt (s.p95)
                  << " p99=" << fmt (s.p99) << " ms" << std::endl;
    }

    processor.releaseResources();

    writeMarkdown (std::cout, results, platform);

    if (jsonOut != juce::File{})
    {
        std::ofstream jf (jsonOut.getFullPathName().toStdString());
        if (jf.is_open())
        {
            writeJson (jf, results, platform);
            std::cerr << "Wrote JSON: " << jsonOut.getFullPathName() << std::endl;
        }
        else
        {
            std::cerr << "Failed to open JSON output: " << jsonOut.getFullPathName() << std::endl;
        }
    }

    if (mdOut != juce::File{})
    {
        std::ofstream mf (mdOut.getFullPathName().toStdString());
        if (mf.is_open())
        {
            writeMarkdown (mf, results, platform);
            std::cerr << "Wrote Markdown: " << mdOut.getFullPathName() << std::endl;
        }
        else
        {
            std::cerr << "Failed to open Markdown output: " << mdOut.getFullPathName() << std::endl;
        }
    }

    return 0;
}

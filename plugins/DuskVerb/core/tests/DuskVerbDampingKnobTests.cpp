// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// The scaler that keeps the damping card (Bass, Mid and Treble Multiply, Low and High
// Crossover) live on the table-voiced engines (DuskVerbDampingScaler.hpp): its ratio, its
// budget, and that it leaves a preset at its own knob values exactly as installed. Whether each
// card knob then changes the render, or is declared dead and drawn dimmed, is checked with
// every other control in DuskVerbControlLivenessTests.cpp.

#include "DuskVerbDSP.hpp"
#include "DuskVerbDampingScaler.hpp"
#include "DuskVerbParamTable.hpp"
#include "DuskVerbPresetEngineConfig.h"
#include "DuskVerbPresets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
int failures = 0;

void checkf(bool ok, const char* fmt, ...)
{
    if (ok) return;
    va_list args;
    va_start(args, fmt);
    std::printf("FAIL: ");
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
    ++failures;
}

using namespace duskverb;

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

const FactoryPreset& presetNamed(const char* name)
{
    for (const auto& preset : dv2FactoryPresets())
        if (std::strcmp(preset.name, name) == 0)
            return preset;
    std::printf("FAIL: no factory preset named '%s'\n", name);
    std::exit(1);
}

// 1. The ratio
void testRatio()
{
    // Exactly 1 at the stored knobs, for every factory program and across the spectrum.
    for (const auto& preset : dv2FactoryPresets())
    {
        const DampingKnobs s = storedDampingKnobs(preset);
        for (float hz = 20.0f; hz < 24000.0f; hz *= 1.1f)
            checkf(decayRatio(s, s, hz) == 1.0f, "'%s': r(%.0f Hz) is 1 at the stored knobs", preset.name, hz);
    }

    const DampingKnobs s { 1.2f, 0.9f, 0.7f, 400.0f, 5000.0f };
    // A multiplier scales its own band fully, nothing outside the crossfades, and nothing
    // anywhere in the opposite direction (each weight is non-negative).
    struct Move { float DampingKnobs::*knob; float deepHz; };
    const Move moves[] = { { &DampingKnobs::bass, 40.0f }, { &DampingKnobs::mid, 1400.0f },
                           { &DampingKnobs::treble, 18000.0f } };
    for (const Move& m : moves)
    {
        float last = 0.0f;
        for (float scale : { 0.5f, 0.8f, 1.25f, 2.0f })
        {
            DampingKnobs c = s;
            c.*m.knob = s.*m.knob * scale;
            const float deep = decayRatio(c, s, m.deepHz);
            checkf(std::fabs(deep - scale) < 1e-5f, "a multiplier x%.2f scales its band's centre by %.6f", scale, deep);
            checkf(deep > last, "r rises with its multiplier (%.6f after %.6f)", deep, last);
            last = deep;
            for (float hz = 20.0f; hz < 24000.0f; hz *= 1.05f)
            {
                const float r = decayRatio(c, s, hz);
                checkf(scale > 1.0f ? r >= 1.0f : r <= 1.0f,
                       "raising a multiplier never shortens a band (x%.2f, %.0f Hz -> %.6f)", scale, hz, r);
            }
        }
    }
    // Bands no moved knob reaches keep exactly 1, so their table entries are never re-pushed.
    {
        DampingKnobs c = s;
        c.treble = 0.35f;
        checkf(decayRatio(c, s, 63.0f) == 1.0f && decayRatio(c, s, 2000.0f) == 1.0f,
               "a Treble move leaves the bass and mid octaves at exactly 1");
    }
    // A crossover move hands the octaves between the old and new split to the other band.
    {
        DampingKnobs c = s;
        c.lowX = 1600.0f;                              // two octaves up from 400 Hz
        checkf(decayRatio(c, s, 63.0f) == 1.0f, "a Low Crossover move leaves the deep bass at 1");
        checkf(decayRatio(c, s, 2500.0f) == 1.0f, "a Low Crossover move leaves the upper mid at 1");
        const float between = decayRatio(c, s, 800.0f);   // fully mid before, fully bass after
        checkf(std::fabs(between - s.bass / s.mid) < 1e-5f,
               "800 Hz moves from the mid to the bass band (r %.6f, want %.6f)", between, s.bass / s.mid);
        DampingKnobs h = s;
        h.highX = 1250.0f;                             // two octaves down from 5 kHz
        const float handed = decayRatio(h, s, 2500.0f);
        checkf(std::fabs(handed - s.treble / s.mid) < 1e-5f,
               "2.5 kHz moves from the mid to the treble band (r %.6f, want %.6f)", handed, s.treble / s.mid);
        // Continuous in the crossover: a small move is a small change.
        DampingKnobs n = s;
        n.lowX = 404.0f;
        checkf(std::fabs(decayRatio(n, s, 400.0f) - 1.0f) < 0.01f, "a 1 %% crossover move changes r by under 1 %%");
    }
}

// 2. The scaler on a real engine
struct EngineUnderTest
{
    std::unique_ptr<DuskVerbEngine> engine = std::make_unique<DuskVerbEngine>();
    DampingScaler scaler;
    DampingKnobs stored;
    int algorithm;

    explicit EngineUnderTest(const FactoryPreset& preset) : algorithm(preset.algorithm)
    {
        engine->prepare(kSr, 4096);
        engine->setAlgorithm(preset.algorithm);
        preset.applyEngineConfig(*engine);
        scaler.prepare(kSr);
        stored = storedDampingKnobs(preset);
        scaler.capture(*engine, stored, true);
    }
    void blocks(const DampingKnobs& knobs, int count)
    {
        for (int i = 0; i < count; ++i) scaler.update(*engine, algorithm, knobs, kBlock);
    }
};

template <typename Get>
int countDiffering(Get&& get, const float* against, int n)
{
    int count = 0;
    for (int k = 0; k < n; ++k) count += get(k) != against[k] ? 1 : 0;
    return count;
}

void testScalerOnEngines()
{
    constexpr int kOct = DampingScaler::kOctaves;

    // AccurateHall (Vocal Plate): a costly table, so live moves are budgeted.
    {
        EngineUnderTest e(presetNamed("Vocal Plate"));
        const auto table = [&e](int k) { return e.engine->getAccurateHallOctaveT60(k); };
        float base[kOct];
        for (int k = 0; k < kOct; ++k) base[k] = table(k);
        checkf(e.engine->isAccurateHallOctaveT60Active(), "Vocal Plate installs an AccurateHall table");

        e.blocks(e.stored, 8);
        checkf(countDiffering(table, base, kOct) == 0, "at the stored knobs the scaler leaves the table as installed");

        DampingKnobs c = e.stored;
        c.treble *= 0.5f;
        c.bass *= 1.5f;
        float want[kOct];
        int affected = 0;
        for (int k = 0; k < kOct; ++k)
        {
            want[k] = base[k] * decayRatio(c, e.stored, DampingScaler::kOctaveHz[k]);
            affected += want[k] != base[k] ? 1 : 0;
        }
        checkf(affected >= 5, "a Bass and Treble move reaches at least five octaves (%d)", affected);
        e.blocks(c, 1);
        checkf(countDiffering(table, base, kOct) == 1, "a 256-sample block pushes one AccurateHall band");
        e.blocks(c, 40);
        checkf(countDiffering(table, want, kOct) == 0, "the table converges on base x r(f)");
        e.blocks(e.stored, 40);
        checkf(countDiffering(table, base, kOct) == 0, "back at the stored knobs the table is the preset's again, bit for bit");

        // Short blocks earn one band per 5 ms of audio, not one per block.
        {
            int pushed = 0;
            float before[kOct];
            for (int i = 0; i < 16; ++i)                // 16 x 64 samples = 21.3 ms
            {
                for (int k = 0; k < kOct; ++k) before[k] = table(k);
                e.scaler.update(*e.engine, e.algorithm, c, 64);
                pushed += countDiffering(table, before, kOct);
            }
            checkf(pushed >= 4 && pushed <= 5, "64-sample blocks push one band per 5 ms (%d in 21 ms)", pushed);
            for (int i = 0; i < 200; ++i) e.scaler.update(*e.engine, e.algorithm, c, 64);
            checkf(countDiffering(table, want, kOct) == 0, "64-sample blocks still converge on base x r(f)");
            e.blocks(e.stored, 40);
        }

        // Straight after a config install (a recalled session with moved knobs) every band
        // goes at once.
        e.scaler.capture(*e.engine, e.stored, true);
        e.blocks(c, 1);
        checkf(countDiffering(table, want, kOct) == 0, "the first block after a capture pushes the whole table");
    }

    // The plates (Drum Plate): the same budget.
    {
        EngineUnderTest e(presetNamed("Drum Plate"));
        const auto table = [&e](int k) { return e.engine->getDattorroOctaveT60(k); };
        float base[kOct];
        for (int k = 0; k < kOct; ++k) base[k] = table(k);
        checkf(e.engine->isDattorroOctaveT60Active(), "Drum Plate installs a plate octave table");
        e.blocks(e.stored, 1);
        DampingKnobs c = e.stored;
        c.mid *= 2.0f;
        e.blocks(c, 1);
        checkf(countDiffering(table, base, kOct) == 1, "a 256-sample block pushes one plate band");
        e.blocks(e.stored, 40);
        checkf(countDiffering(table, base, kOct) == 0, "the plate table returns to the preset's");
        // A 2048-sample block carries eight bands: the Mid move's six go in one.
        float want[kOct];
        for (int k = 0; k < kOct; ++k) want[k] = base[k] * decayRatio(c, e.stored, DampingScaler::kOctaveHz[k]);
        e.scaler.update(*e.engine, e.algorithm, c, 2048);
        checkf(countDiffering(table, want, kOct) == 0, "a large block carries several bands");
    }

    // DenseHall (Bright Hall): cheap, follows every block in full.
    {
        EngineUnderTest e(presetNamed("Bright Hall"));
        const auto table = [&e](int k) { return e.engine->getDenseHallOctaveT60(k); };
        float base[kOct];
        for (int k = 0; k < kOct; ++k) base[k] = table(k);
        e.blocks(e.stored, 1);
        DampingKnobs c = e.stored;
        c.bass *= 2.0f;
        c.treble *= 0.5f;
        float want[kOct];
        for (int k = 0; k < kOct; ++k) want[k] = base[k] * decayRatio(c, e.stored, DampingScaler::kOctaveHz[k]);
        e.blocks(c, 1);
        checkf(countDiffering(table, want, kOct) == 0, "a DenseHall table follows the knobs within one block");
    }

    // ParallelMultiband (Vocal Hall): every band in one block, level/direct/width kept; and a
    // neutral config does not let this scaler's scaling leak into the restored session.
    {
        EngineUnderTest e(presetNamed("Vocal Hall"));
        constexpr int kB = DampingScaler::kPmbBands;
        float t60[kB], level[kB], direct[kB], width[kB];
        for (int b = 0; b < kB; ++b)
        {
            t60[b] = e.engine->getPmbBandT60(b);       level[b] = e.engine->getPmbBandLevel(b);
            direct[b] = e.engine->getPmbBandDirect(b); width[b] = e.engine->getPmbBandWidth(b);
        }
        DampingKnobs c = e.stored;
        c.mid *= 0.5f;
        e.blocks(c, 1);
        int wrong = 0;
        for (int b = 0; b < kB; ++b)
        {
            const float want = std::clamp(t60[b] * decayRatio(c, e.stored, DampingScaler::kPmbHz[b]), 0.05f, 30.0f);
            wrong += e.engine->getPmbBandT60(b) != want ? 1 : 0;
            wrong += e.engine->getPmbBandLevel(b) != level[b] || e.engine->getPmbBandDirect(b) != direct[b]
                  || e.engine->getPmbBandWidth(b) != width[b] ? 1 : 0;
        }
        checkf(wrong == 0, "ParallelMultiband bands follow within one block, other band settings kept (%d wrong)", wrong);

        e.engine->reapplyNeutralEngineConfig();
        e.scaler.capture(*e.engine, c, false);
        e.blocks(c, 1);
        const auto bandT60 = [&e](int b) { return e.engine->getPmbBandT60(b); };
        checkf(countDiffering(bandT60, t60, kB) == 0,
               "a neutral config restores the unscaled bands, so no earlier knob scaling leaks");
    }

    // The QuadTank split: untouched at the stored knobs and as the -1 sentinel.
    {
        const DampingKnobs s = storedDampingKnobs(presetNamed("79 Vocal Chamber"));
        DampingScaler scaler;
        auto engine = std::make_unique<DuskVerbEngine>();
        scaler.capture(*engine, s, true);
        checkf(scaler.quadMultiplier(0.11f, DampingScaler::kQuadHiMidHz, s) == 0.11f,
               "the QuadTank hi-mid multiplier is pushed raw at the stored knobs");
        checkf(scaler.quadMultiplier(-1.0f, DampingScaler::kQuadAirHz, DampingKnobs {}) == -1.0f,
               "the -1 'follow Treble' sentinel passes through");
        DampingKnobs c = s;
        c.treble *= 0.5f;
        checkf(scaler.quadMultiplier(0.22f, DampingScaler::kQuadAirHz, c) < 0.22f * 0.51f,
               "halving Treble halves the QuadTank air multiplier");
    }
}

} // namespace

int main()
{
    primeTuningEnvCache();
    testRatio();
    testScalerOnEngines();

    if (failures == 0) std::printf("DuskVerbDampingKnobTests: all checks passed\n");
    else               std::printf("DuskVerbDampingKnobTests: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}

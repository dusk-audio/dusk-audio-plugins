// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The band- and filter-frequency rules of 4K EQ 2 without a host
// (dusk-audio-plugins#288): which of a band's or filter's two parameters wins,
// where the factory presets put each band and filter by the core's definition,
// what the read-out shows as gain moves, user preset files written before and
// after #288, and that writes arriving on two threads at once never lose one
// another's selector update.

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "FourKEQBandFrequency.hpp"
#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"
#include "FourKEQPresetRuntime.hpp"
#include "FourKEQUserPresetFile.hpp"

namespace duskaudio
{
struct FourKEQDSPTestAccess
{
    static FourKEQDSP::SectionDesigns designs(const FourKEQDSP::CurveControls& c) noexcept
    {
        return FourKEQDSP::designSections(FourKEQDSP::coeffInputsFor(c));
    }

    // The band, pair-correction and filter sections the DSP is running.
    static std::array<BiquadCoeffs, 13> running(const FourKEQDSP& dsp) noexcept
    {
        const auto& c = dsp.ch[0];
        return { c.lf.coeffs(), c.lm.coeffs(), c.hm.coeffs(), c.hf.coeffs(),
                 c.lowCorrection1.coeffs(), c.lowCorrection2.coeffs(), c.lowCorrection3.coeffs(),
                 c.highCorrection1.coeffs(), c.highCorrection2.coeffs(), c.highCorrection3.coeffs(),
                 c.hpf1.coeffs(), c.hpf2.coeffs(), c.lpf.coeffs() };
    }
};
} // namespace duskaudio

namespace
{
using duskaudio::FourKEQDSP;
using duskaudio::FourKEQDSPTestAccess;
using Band = FourKEQDSP::Band;
using duskaudio::BiquadCoeffs;

int gChecks = 0, gFailures = 0;

#define CHECK(cond, ...)                                                              \
    do                                                                                \
    {                                                                                 \
        ++gChecks;                                                                    \
        if (!(cond))                                                                  \
        {                                                                             \
            ++gFailures;                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
            std::fprintf(stderr, __VA_ARGS__);                                        \
            std::fputc('\n', stderr);                                                 \
        }                                                                             \
    } while (false)

struct Values
{
    float v[kParamCount];
    Values() { for (uint32_t i = 0; i < kParamCount; ++i) v[i] = kFourKParams[i].def; }
};

void testLastWriteWins()
{
    Values s;
    CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 0u, "a fresh state follows a legacy dial");
    fkStoreParam(s.v, kHmFreq, 5000.0f);
    CHECK(fkBandFollowsLegacyDial(s.v, 2), "a legacy HM dial write did not take the band");
    CHECK(!fkBandFollowsLegacyDial(s.v, 0) && !fkBandFollowsLegacyDial(s.v, 3), "other bands moved");
    CHECK(std::abs(fkBandHz(s.v, 2) - FourKEQDSP::hzForCalibratedEqControl(5000.0f, Band::HM, false, true)) < 1e-3f,
          "a legacy HM band reads %.1f Hz", fkBandHz(s.v, 2));
    fkStoreParam(s.v, kHmHz, 6500.0f);
    CHECK(!fkBandFollowsLegacyDial(s.v, 2) && fkBandHz(s.v, 2) == 6500.0f, "an HM Hz write did not take the band back");
    fkStoreParam(s.v, kLegacyDialBands, 9.0f);
    CHECK(fkBandFollowsLegacyDial(s.v, 0) && fkBandFollowsLegacyDial(s.v, 3) && !fkBandFollowsLegacyDial(s.v, 2),
          "restoring kLegacyDialBands did not select the bands it names");
    fkStoreParam(s.v, kLegacyDialBands, 99.0f);
    CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 15u, "an out-of-range selector was not clamped");
    fkStoreParam(s.v, kLegacyDialBands, std::nanf(""));
    CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 0u, "a NaN selector is not 'no legacy bands'");

    Values f;
    CHECK(fkLegacyDialFilterBits(f.v[kLegacyDialFilters]) == 0u, "a fresh state follows a legacy filter dial");
    fkStoreParam(f.v, kHpfFreq, 120.0f);
    CHECK(fkFilterFollowsLegacyDial(f.v, 0) && !fkFilterFollowsLegacyDial(f.v, 1),
          "a legacy HPF dial write did not take the HPF alone");
    CHECK(fkLegacyDialBits(f.v[kLegacyDialBands]) == 0u, "a filter write moved a band");
    CHECK(std::abs(fkFilterHz(f.v, 0) - FourKEQDSP::hzForCalibratedFilterControl(120.0f, true, false)) < 1e-3f,
          "a legacy HPF reads %.1f Hz", fkFilterHz(f.v, 0));
    fkStoreParam(f.v, kHmFreq, 5000.0f);
    CHECK(fkFilterFollowsLegacyDial(f.v, 0) && fkBandFollowsLegacyDial(f.v, 2), "a band write moved a filter");
    fkStoreParam(f.v, kHpfHz, 80.0f);
    CHECK(!fkFilterFollowsLegacyDial(f.v, 0) && fkFilterHz(f.v, 0) == 80.0f, "an HPF Hz write did not take the HPF back");
    fkStoreParam(f.v, kLpfFreq, 9000.0f);
    fkStoreParam(f.v, kLegacyDialFilters, 1.0f);
    CHECK(fkFilterFollowsLegacyDial(f.v, 0) && !fkFilterFollowsLegacyDial(f.v, 1),
          "restoring kLegacyDialFilters did not select the filters it names");
    fkStoreParam(f.v, kLegacyDialFilters, 99.0f);
    CHECK(fkLegacyDialFilterBits(f.v[kLegacyDialFilters]) == 3u, "an out-of-range filter selector was not clamped");
    fkStoreParam(f.v, kLegacyDialFilters, std::nanf(""));
    CHECK(fkLegacyDialFilterBits(f.v[kLegacyDialFilters]) == 0u, "a NaN filter selector is not 'no legacy filters'");
}

// A stated selector carries a flag that only marks the write: the bits under
// it decide, frequency writes keep it, and a second statement of the same bits
// takes the other flag, so a host that passes on only changed values still
// delivers it.
void testStatedSelector()
{
    Values s;
    fkStoreParam(s.v, kLegacyDialBands, (float)(kSelectorStated | 9u));
    CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 9u, "a stated selector selects %u", fkLegacyDialBits(s.v[kLegacyDialBands]));
    fkStoreParam(s.v, kLmFreq, 800.0f);
    CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 11u, "a dial write under a stated selector");
    CHECK(((uint32_t)s.v[kLegacyDialBands] & kSelectorStated) != 0u, "a dial write dropped the selector's flag");
    fkStoreParam(s.v, kLfHz, 120.0f);
    CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 10u, "an Hz write under a stated selector");

    const float first = fkStatedSelector(0u, 0.0f, kLegacyDialBandsMax);
    const float second = fkStatedSelector(0u, first, kLegacyDialBandsMax);
    const float third = fkStatedSelector(0u, second, kLegacyDialBandsMax);
    CHECK(first != 0.0f && second != first && third != second, "stated selectors %g, %g, %g repeat", first, second, third);
    CHECK(fkLegacyDialBits(first) == 0u && fkLegacyDialBits(second) == 0u && fkLegacyDialBits(third) == 0u,
          "a stated selector changed the bits it states");
    const float filters = fkStatedSelector(2u, kLegacyDialFiltersMax, kLegacyDialFiltersMax);
    CHECK(fkLegacyDialFilterBits(filters) == 2u && filters <= kLegacyDialFiltersMax,
          "a stated filter selector reads %g", filters);

    // A factory preset states its selectors, so it takes every band and
    // filter back from its dial and leaves the dials where they were.
    Values dials;
    for (int b = 0; b < 4; ++b)
        fkStoreParam(dials.v, kFourKEQBands[b].legacyDial, kFourKParams[kFourKEQBands[b].legacyDial].min);
    fkStoreParam(dials.v, kHpfFreq, 120.0f);
    fkStoreParam(dials.v, kLpfFreq, 9000.0f);
    forEachFourKEQFactoryPresetParam(0, [&](uint32_t id, float v) { fkStoreParam(dials.v, id, v); });
    CHECK(fkLegacyDialBits(dials.v[kLegacyDialBands]) == 0u && fkLegacyDialFilterBits(dials.v[kLegacyDialFilters]) == 0u,
          "a factory preset left a band or filter on its dial");
    for (int b = 0; b < 4; ++b)
        CHECK(dials.v[kFourKEQBands[b].legacyDial] == kFourKParams[kFourKEQBands[b].legacyDial].min,
              "a factory preset moved band %d's legacy dial", b);
    CHECK(dials.v[kHpfFreq] == 120.0f && dials.v[kLpfFreq] == 9000.0f, "a factory preset moved a legacy filter dial");
}

// A selector word whose every operation is one step, with a hook before each,
// so a test can land another write between any two steps a write takes.
struct SteppedWord
{
    uint32_t v;
    static inline std::function<void()> step;

    void before() const { if (step) step(); }
    uint32_t load(std::memory_order) const { before(); return v; }
    void store(uint32_t x, std::memory_order) { before(); v = x; }
    uint32_t exchange(uint32_t x, std::memory_order) { before(); const uint32_t old = v; v = x; return old; }
    uint32_t fetch_or(uint32_t x, std::memory_order) { before(); const uint32_t old = v; v |= x; return old; }
    uint32_t fetch_and(uint32_t x, std::memory_order) { before(); const uint32_t old = v; v &= x; return old; }
    bool compare_exchange_weak(uint32_t& expected, uint32_t desired, std::memory_order, std::memory_order)
    {
        before();
        if (v != expected) { expected = v; return false; }
        v = desired;
        return true;
    }
};

// A host can write parameters from two threads at once: a state or program
// loading on the main thread, automation on the audio thread. So any write can
// land between two steps of another's selector update. Every write, with every
// other landing before each of its steps in turn, must leave the selectors as
// the two writes one after the other do, never as if one had not happened.
void testSelectorWritesNeverLoseOneAnother()
{
    using Stepped = FourKEQSelectorsOf<SteppedWord>;
    struct Write { uint32_t index; float value; };
    std::vector<Write> writes;
    for (const FourKEQBandIds& ids : kFourKEQBands)
        for (const uint32_t id : { ids.legacyDial, ids.hz })
            writes.push_back({ id, kFourKParams[id].def });
    for (const FourKEQFilterIds& ids : kFourKEQFilters)
        for (const uint32_t id : { ids.legacyDial, ids.hz })
            writes.push_back({ id, kFourKParams[id].def });
    for (const uint32_t selector : { (uint32_t)kLegacyDialBands, (uint32_t)kLegacyDialFilters })
    {
        writes.push_back({ selector, (float)(kSelectorStated | 1u) });
        writes.push_back({ selector, (float)(kSelectorStatedAlt | 2u) });
    }
    const uint32_t starts[][2] = { { 0u, 0u },
                                   { kSelectorStated | 15u, kSelectorStated | 3u },
                                   { kSelectorStatedAlt | 6u, kSelectorStatedAlt | 1u } };

    int interleavings = 0, lost = 0;
    for (const auto& start : starts)
        for (const Write& a : writes)
            for (const Write& b : writes)
            {
                Values sequential;
                sequential.v[kLegacyDialBands] = (float)start[0];
                sequential.v[kLegacyDialFilters] = (float)start[1];
                fkStoreParam(sequential.v, b.index, b.value);
                fkStoreParam(sequential.v, a.index, a.value);
                const uint32_t bands = (uint32_t)sequential.v[kLegacyDialBands];
                const uint32_t filters = (uint32_t)sequential.v[kLegacyDialFilters];

                FourKEQSelectors plugin;
                plugin.bands.store(start[0]);
                plugin.filters.store(start[1]);
                plugin.record(b.index, b.value);
                plugin.record(a.index, a.value);
                CHECK(plugin.value(kLegacyDialBands) == bands && plugin.value(kLegacyDialFilters) == filters,
                      "param %u then %u: the plugin's selectors %u/%u, a copy's %u/%u", b.index, a.index,
                      plugin.value(kLegacyDialBands), plugin.value(kLegacyDialFilters), bands, filters);

                int steps = 0;
                {
                    Stepped s { { start[0] }, { start[1] } };
                    SteppedWord::step = [&] { ++steps; };
                    s.record(a.index, a.value);
                }
                CHECK(steps > 0, "param %u moved no selector", a.index);
                for (int k = 0; k < steps; ++k)
                {
                    Stepped s { { start[0] }, { start[1] } };
                    int step = 0;
                    bool landing = false;
                    SteppedWord::step = [&] {
                        if (landing || step++ != k)
                            return;
                        landing = true;
                        s.record(b.index, b.value);
                        landing = false;
                    };
                    s.record(a.index, a.value);
                    ++interleavings;
                    lost += (s.bands.v != bands || s.filters.v != filters) ? 1 : 0;
                    CHECK(s.bands.v == bands && s.filters.v == filters,
                          "param %u landing before step %d of param %u: selectors %u/%u, in order %u/%u",
                          b.index, k, a.index, s.bands.v, s.filters.v, bands, filters);
                }
            }
    SteppedWord::step = nullptr;

    for (uint32_t i = 0; i < kParamCount; ++i)
    {
        if (fkSelectorWrite(i, 1.0f).op != FourKEQSelectorWrite::None)
            continue;
        Stepped s { { 7u }, { 3u } };
        int steps = 0;
        SteppedWord::step = [&] { ++steps; };
        const bool moved = s.record(i, 1.0f);
        CHECK(!moved && steps == 0 && s.bands.v == 7u && s.filters.v == 3u, "param %u touched a selector", i);
    }
    SteppedWord::step = nullptr;
    std::printf("[6] selector writes: %d interleavings of %zu writes, %d lost one\n", interleavings, writes.size(), lost);
}

// The same on two real threads (and a check for -fsanitize=thread): each
// thread moving its own band and filter, then a state stating the band
// selector while automation moves LM.
void testSelectorWritesRace()
{
    constexpr int kWrites = 100000;
    std::atomic<int> ready { 0 }, wrong { 0 };
    auto start = [&ready] {
        ready.fetch_add(1);
        while (ready.load() < 2)
            std::this_thread::yield();
    };

    FourKEQSelectors s;
    s.record(kLegacyDialBands, (float)kSelectorStated);
    s.record(kLegacyDialFilters, (float)kSelectorStated);
    auto owner = [&](int band, int filter) {
        start();
        for (int i = 0; i < kWrites; ++i)
        {
            const uint32_t dial = (uint32_t)(i & 1);
            s.record(dial ? kFourKEQBands[band].legacyDial : kFourKEQBands[band].hz, 1000.0f);
            s.record(dial ? kFourKEQFilters[filter].legacyDial : kFourKEQFilters[filter].hz, 100.0f);
            if (((s.bandBits() >> band) & 1u) != dial || ((s.filterBits() >> filter) & 1u) != dial)
                wrong.fetch_add(1, std::memory_order_relaxed);
        }
    };
    std::thread first(owner, 0, 0), second(owner, 1, 1);
    first.join();
    second.join();
    const int moved = wrong.load();
    CHECK(moved == 0, "%d writes found their own band or filter moved by the other thread", moved);
    CHECK(s.value(kLegacyDialBands) == (kSelectorStated | 3u) && s.value(kLegacyDialFilters) == (kSelectorStated | 3u),
          "selectors ended %u/%u", s.value(kLegacyDialBands), s.value(kLegacyDialFilters));

    FourKEQSelectors t;
    ready = 0;
    wrong = 0;
    std::atomic<bool> loading { true };
    std::thread automation([&] {
        start();
        for (int i = 0; loading.load(std::memory_order_relaxed); ++i)
            t.record((i & 1) ? kLmFreq : kLmHz, 1000.0f);
    });
    start();
    for (int i = 0; i < kWrites; ++i)
    {
        const uint32_t stated = ((i & 16) ? kSelectorStatedAlt : kSelectorStated) | ((uint32_t)i & 13u);
        t.record(kLegacyDialBands, (float)stated);
        if ((t.value(kLegacyDialBands) & ~2u) != stated)
            wrong.fetch_add(1, std::memory_order_relaxed);
    }
    loading = false;
    automation.join();
    CHECK(wrong.load() == 0, "%d stated selectors lost a bit or their flag to LM automation", wrong.load());
    std::printf("[7] selector writes on two threads, %d each: %d moved another's band or filter, "
                "%d stated selectors lost to automation\n", kWrites, moved, wrong.load());
}

void testLegacyDialDefaults()
{
    const float shipped[4] = { 200.f, 1000.f, 3000.f, 8000.f };
    for (int b = 0; b < 4; ++b)
    {
        const FourKEQBandIds& ids = kFourKEQBands[b];
        const float dial = kFourKParams[ids.legacyDial].def;
        const float hz = FourKEQDSP::hzForCalibratedEqControl(dial, ids.band, false, ids.bellSwitch < 0 || b == 3);
        CHECK(std::abs(hz / kFourKParams[ids.hz].def - 1.0f) < 1.0e-4f,
              "band %d legacy dial default %.4f plays %.2f Hz, Hz default %.0f", b, dial, hz, kFourKParams[ids.hz].def);
        CHECK(dial != shipped[b], "band %d legacy dial default is 1.0.5's", b);
    }
    const float shippedFilters[2] = { 16.f, 15201.f };
    for (int f = 0; f < 2; ++f)
    {
        const FourKEQFilterIds& ids = kFourKEQFilters[f];
        const float dial = kFourKParams[ids.legacyDial].def;
        const float hz = FourKEQDSP::hzForCalibratedFilterControl(dial, ids.highPass, false);
        CHECK(std::abs(hz / kFourKParams[ids.hz].def - 1.0f) < 1.0e-4f,
              "filter %d legacy dial default %.4f plays %.2f Hz, Hz default %.0f", f, dial, hz, kFourKParams[ids.hz].def);
        CHECK(dial != shippedFilters[f], "filter %d legacy dial default is 1.0.5's", f);
    }
}

// The Hz a designed band plays, by the core's definition: a bell's centre, a
// shelf's corner, at the reference gain.
float designedHz(const FourKEQDSP::SectionDesign& d, Band band, bool black, bool bell)
{
    if (bell || band == Band::LM || band == Band::HM)
        return d.freq;
    const float gainDb = FourKEQDSP::calibratedEqGain(FourKEQDSP::kEqReferenceGainDb, band, black, false);
    const float sqrtA = std::pow(10.0f, gainDb / 80.0f);
    return band == Band::HF ? d.freq * sqrtA : d.freq / sqrtA;
}

void testFactoryPresetsStateTheirHz()
{
    std::printf("[2] factory presets, band and filter Hz by the core's definition (was: what the pre-#288 build played)\n");
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const FourKEQPreset& p = kFactoryPresets[i];
        Values s;
        forEachFourKEQFactoryPresetParam(i, [&](uint32_t id, float v) { fkStoreParam(s.v, id, v); });
        const float stated[4] = { p.lfFreq, p.lmFreq, p.hmFreq, p.hfFreq };
        CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 0u, "%s leaves a band on its legacy dial", p.name);

        // Designed at the reference gain, where the definition is exact.
        FourKEQDSP::CurveControls c;
        c.black = p.eqType > 0.5f;
        c.bandFrequenciesInHz = true;
        c.lfBell = p.lfBell; c.hfBell = p.hfBell; c.lmQ = p.lmQ; c.hmQ = p.hmQ;
        c.lfGain = c.lmGain = c.hmGain = c.hfGain = FourKEQDSP::kEqReferenceGainDb;
        c.lfFreq = fkBandHz(s.v, 0); c.lmFreq = fkBandHz(s.v, 1);
        c.hmFreq = fkBandHz(s.v, 2); c.hfFreq = fkBandHz(s.v, 3);
        const auto d = FourKEQDSPTestAccess::designs(c);

        // What the pre-#288 build played: the Hz inverted through the
        // gain-dependent dial law, clamped at the dial's ends.
        float before[4];
        for (int b = 0; b < 4; ++b)
        {
            const FourKEQBandIds& ids = kFourKEQBands[b];
            const bool bell = fkBandIsBell(s.v, b);
            const float dial = FourKEQDSP::controlForCalibratedEqFrequency(
                stated[b], s.v[ids.gain], ids.band, c.black, bell);
            before[b] = FourKEQDSP::hzForCalibratedEqControl(dial, ids.band, c.black, bell);
            const float got = designedHz(d.bands[b], ids.band, c.black, bell);
            CHECK(std::abs(got / stated[b] - 1.0f) < 1.0e-4f, "%s band %d designs %.1f Hz, states %.1f",
                  p.name, b, got, stated[b]);
            CHECK(std::abs(fkBandHz(s.v, b) - stated[b]) < 1.0e-3f, "%s band %d reads %.1f Hz", p.name, b, fkBandHz(s.v, b));
        }
        std::printf("  %-22s %-5s LF %5.0f%s (%4.0f)  LM %5.0f (%5.0f)  HM %5.0f (%5.0f)  HF %5.0f%s (%5.0f)\n",
                    p.name, c.black ? "Black" : "Brown",
                    stated[0], p.lfBell > 0.5f ? "b" : "s", before[0], stated[1], before[1],
                    stated[2], before[2], stated[3], p.hfBell > 0.5f ? "b" : "s", before[3]);

        // The filters: each stated -3 dB point, where the pre-#288 build put
        // the design frequency at the stated number through the dial.
        const float statedFilters[2] = { p.hpfFreq, p.lpfFreq };
        CHECK(fkLegacyDialFilterBits(s.v[kLegacyDialFilters]) == 0u, "%s leaves a filter on its legacy dial", p.name);
        CHECK((s.v[kHpfEnabled] > 0.5f) == (p.hpfFreq > 16.5f) && (s.v[kLpfEnabled] > 0.5f) == (p.lpfFreq < 15200.5f),
              "%s switches the wrong filters in", p.name);
        float beforeFilters[2];
        for (int f = 0; f < 2; ++f)
        {
            const bool highPass = kFourKEQFilters[f].highPass;
            CHECK(std::abs(fkFilterHz(s.v, f) - statedFilters[f]) < 1.0e-3f, "%s filter %d reads %.1f Hz", p.name, f, fkFilterHz(s.v, f));
            const float design = FourKEQDSP::calibratedFilterFrequencyForHz(fkFilterHz(s.v, f), highPass, c.black);
            CHECK(std::abs(design * FourKEQDSP::filterCornerRatio(highPass, c.black) / statedFilters[f] - 1.0f) < 1.0e-5f,
                  "%s filter %d designs %.1f Hz", p.name, f, design);
            beforeFilters[f] = FourKEQDSP::hzForCalibratedFilterControl(
                FourKEQDSP::controlForCalibratedFilterFrequency(statedFilters[f], highPass, c.black), highPass, c.black);
        }
        if (s.v[kHpfEnabled] > 0.5f || s.v[kLpfEnabled] > 0.5f)
            std::printf("  %-22s       HPF %5.0f (%5.1f)  LPF %5.0f (%5.0f)\n", "", statedFilters[0], beforeFilters[0],
                        statedFilters[1], beforeFilters[1]);
    }
}

void testReadoutIgnoresGain()
{
    int cases = 0;
    double oldWorst = 0.0;
    for (int black = 0; black < 2; ++black)
        for (int bell = 0; bell < 2; ++bell)
            for (int b = 0; b < 4; ++b)
                for (int legacy = 0; legacy < 2; ++legacy)
                {
                    const FourKEQBandIds& ids = kFourKEQBands[b];
                    Values s;
                    s.v[kEqType] = (float)black;
                    s.v[kLfBell] = s.v[kHfBell] = (float)bell;
                    const float mid = std::sqrt(kFourKParams[ids.hz].min * kFourKParams[ids.hz].max);
                    fkStoreParam(s.v, legacy ? ids.legacyDial : ids.hz, mid);
                    char first[32];
                    s.v[ids.gain] = -15.0f;
                    std::snprintf(first, sizeof(first), kFourKBandHzFormat, fkBandHz(s.v, b));
                    const float atMinus15 = FourKEQDSP::calibratedEqFrequency(mid, -15.0f, ids.band, black, fkBandIsBell(s.v, b));
                    for (float g = -15.0f; g <= 15.0f; g += 0.5f)
                    {
                        s.v[ids.gain] = g;
                        char text[32];
                        std::snprintf(text, sizeof(text), kFourKBandHzFormat, fkBandHz(s.v, b));
                        CHECK(std::string(text) == first, "band %d %s: read-out %s at %+.1f dB, %s at -15 dB",
                              b, legacy ? "legacy dial" : "Hz", text, g, first);
                        ++cases;
                        const float old = FourKEQDSP::calibratedEqFrequency(mid, g, ids.band, black, fkBandIsBell(s.v, b));
                        oldWorst = std::max(oldWorst, std::abs((double)old / atMinus15 - 1.0));
                    }
                }
    std::printf("[3] read-out across gain: %d settings, one text per band; the pre-#288 read-out moved by up to %.0f%%\n",
                cases, 100.0 * oldWorst);
}

void testCurveDrawsWhatPlays()
{
    // Mixed states included: a legacy dial inside the flat run at the end of
    // its table plays a different pair correction than its Hz equivalent.
    struct Case { const char* name; int black, lfBell, hfBell; uint32_t writes[6]; float values[6]; };
    const Case cases[] = {
        { "all Hz",                  0, 0, 0, { kLfHz, kLmHz, kHmHz, kHfHz, kHpfHz, kLpfHz },
                                              { 90.f, 700.f, 7000.f, 1500.f, 80.f, 9000.f } },
        { "all legacy dial",         1, 1, 0, { kLfFreq, kLmFreq, kHmFreq, kHfFreq, kHpfFreq, kLpfFreq },
                                              { 33.f, 230.f, 650.f, 16000.f, 350.f, 3000.f } },
        { "legacy LF/LM at the ends", 1, 1, 0, { kLfFreq, kLmFreq, kHmHz, kHfHz, kHpfFreq, kLpfHz },
                                              { 33.f, 230.f, 7000.f, 1500.f, 16.f, 15201.f } },
        { "legacy HM/HF",            0, 0, 1, { kLfHz, kLmHz, kHmFreq, kHfFreq, kHpfHz, kLpfFreq },
                                              { 450.f, 2500.f, 7000.f, 1500.f, 16.f, 12800.f } },
    };
    int sections = 0;
    for (const Case& k : cases)
    {
        Values s;
        s.v[kEqType] = (float)k.black;
        s.v[kLfBell] = (float)k.lfBell;
        s.v[kHfBell] = (float)k.hfBell;
        s.v[kLfGain] = 9.f; s.v[kLmGain] = -6.f; s.v[kHmGain] = 9.f; s.v[kHfGain] = -4.5f;
        for (int i = 0; i < 6; ++i)
            fkStoreParam(s.v, k.writes[i], k.values[i]);

        FourKEQDSP dsp;
        dsp.setEqType(k.black); dsp.setLfBell(k.lfBell); dsp.setHfBell(k.hfBell);
        dsp.setLfGain(s.v[kLfGain]); dsp.setLmGain(s.v[kLmGain]);
        dsp.setHmGain(s.v[kHmGain]); dsp.setHfGain(s.v[kHfGain]);
        dsp.setHpfEnabled(true); dsp.setLpfEnabled(true);
        fkApplyBandFrequencies(dsp, s.v, fkLegacyDialBits(s.v[kLegacyDialBands]));
        fkApplyFilterFrequencies(dsp, s.v, fkLegacyDialFilterBits(s.v[kLegacyDialFilters]));
        dsp.prepare(48000.0, 64);
        std::vector<float> buf(64, 0.0f);
        float* io[2] = { buf.data(), buf.data() };
        dsp.processBlock(io, io, 2, 64);
        const auto playing = FourKEQDSPTestAccess::running(dsp);

        FourKEQDSP::CurveControls c;
        c.baseSampleRate = 48000.0;
        c.oversampling = 2.0f;
        c.black = k.black; c.lfBell = s.v[kLfBell]; c.hfBell = s.v[kHfBell];
        c.lfGain = s.v[kLfGain]; c.lmGain = s.v[kLmGain]; c.hmGain = s.v[kHmGain]; c.hfGain = s.v[kHfGain];
        c.lmQ = s.v[kLmQ]; c.hmQ = s.v[kHmQ];
        c.hpfEnabled = c.lpfEnabled = true;
        fkSetCurveBandFrequencies(c, s.v);
        fkSetCurveFilterFrequencies(c, s.v);
        const auto drawn = FourKEQDSP::designCurve(c);
        const BiquadCoeffs drawnSections[13] = {
            drawn.bands[0], drawn.bands[1], drawn.bands[2], drawn.bands[3],
            drawn.lowCorrection[0], drawn.lowCorrection[1], drawn.lowCorrection[2],
            drawn.highCorrection[0], drawn.highCorrection[1], drawn.highCorrection[2],
            drawn.hpfFirstOrder, drawn.hpf, drawn.lpf };
        for (int i = 0; i < 13; ++i)
        {
            ++sections;
            CHECK(std::memcmp(&playing[(size_t)i], &drawnSections[i], sizeof(BiquadCoeffs)) == 0,
                  "%s: drawn section %d differs from the one playing", k.name, i);
        }
    }
    std::printf("[5] response curve: %d band, pair-correction and filter sections drawn exactly as they play\n", sections);
}

std::string oldEffectiveHzFile(const Values& s)
{
    // What the pre-#288 editor saved: every band as the gain-dependent
    // frequency its dial played, under the dial parameter's key.
    std::ostringstream f;
    f.imbue(std::locale::classic());
    f << std::setprecision(9);
    f << "name=Old\nformat_version=2\nfrequency_domain=effective_hz\n";
    const bool black = s.v[kEqType] > 0.5f;
    for (uint32_t i : { (uint32_t)kHpfFreq, (uint32_t)kHpfEnabled, (uint32_t)kLpfFreq, (uint32_t)kLpfEnabled,
                        (uint32_t)kLfGain, (uint32_t)kLfBell, (uint32_t)kLmGain, (uint32_t)kLmQ,
                        (uint32_t)kHmGain, (uint32_t)kHmQ, (uint32_t)kHfGain, (uint32_t)kHfBell,
                        (uint32_t)kEqType, (uint32_t)kInputGain, (uint32_t)kOutputGain, (uint32_t)kAutoGain })
    {
        float v = s.v[i];
        if (i == kHpfFreq || i == kLpfFreq)
            v = FourKEQDSP::calibratedFilterFrequency(v, i == kHpfFreq, black);
        f << kFourKParams[i].key << '=' << v << '\n';
    }
    for (int b = 0; b < 4; ++b)
    {
        const FourKEQBandIds& ids = kFourKEQBands[b];
        f << kFourKParams[ids.legacyDial].key << '='
          << FourKEQDSP::calibratedEqFrequency(s.v[ids.legacyDial], s.v[ids.gain], ids.band, black, fkBandIsBell(s.v, b))
          << '\n';
    }
    return f.str();
}

void testUserPresetFiles()
{
    Values old;
    old.v[kEqType] = 1.0f;
    old.v[kHfBell] = 0.0f;
    const float dials[4] = { 380.0f, 290.0f, 6600.0f, 2400.0f };
    const float gains[4] = { 6.0f, -3.0f, 9.0f, 4.5f };
    for (int b = 0; b < 4; ++b)
    {
        old.v[kFourKEQBands[b].legacyDial] = dials[b];
        old.v[kFourKEQBands[b].gain] = gains[b];
    }
    for (const char* domain : { "effective", "control" })
    {
        std::string text = oldEffectiveHzFile(old);
        if (domain[0] == 'c')
        {
            std::ostringstream f;
            f << "name=Old\nformat_version=2\nfrequency_domain=control_hz\neq_type=1\n";
            for (int b = 0; b < 4; ++b)
                f << kFourKParams[kFourKEQBands[b].legacyDial].key << '=' << dials[b] << '\n'
                  << kFourKParams[kFourKEQBands[b].gain].key << '=' << gains[b] << '\n';
            text = f.str();
        }
        std::istringstream in(text);
        std::string name;
        float read[kParamCount];
        CHECK(fkReadUserPreset(in, name, read), "a format 2 (%s_hz) preset was rejected", domain);
        CHECK(fkLegacyDialBits(read[kLegacyDialBands]) == 15u, "a format 2 (%s_hz) preset left a band off its dial", domain);
        for (int b = 0; b < 4; ++b)
            CHECK(std::abs(read[kFourKEQBands[b].legacyDial] / dials[b] - 1.0f) < 1.0e-4f,
                  "format 2 (%s_hz) band %d reads dial %.2f, saved %.2f", domain, b,
                  read[kFourKEQBands[b].legacyDial], dials[b]);

        // Saved again by this build, and read back: the same dials.
        std::ostringstream again;
        fkWriteUserPreset(again, read);
        std::istringstream back(again.str());
        float reread[kParamCount];
        CHECK(fkReadUserPreset(back, name, reread), "a re-saved format 2 preset was rejected");
        for (int b = 0; b < 4; ++b)
            CHECK(fkBandFollowsLegacyDial(reread, b)
                      && reread[kFourKEQBands[b].legacyDial] == read[kFourKEQBands[b].legacyDial],
                  "re-saved band %d did not keep its dial", b);
    }

    Values now;
    fkStoreParam(now.v, kHmHz, 7000.0f);
    fkStoreParam(now.v, kHfHz, 1500.0f);
    fkStoreParam(now.v, kLfFreq, 400.0f);
    std::ostringstream out;
    fkWriteUserPreset(out, now.v);
    std::istringstream in(out.str());
    std::string name;
    float read[kParamCount];
    CHECK(fkReadUserPreset(in, name, read), "a format 4 preset was rejected");
    CHECK(read[kHmHz] == 7000.0f && read[kHfHz] == 1500.0f && !fkBandFollowsLegacyDial(read, 2),
          "format 4 lost its Hz bands");
    CHECK(fkBandFollowsLegacyDial(read, 0) && read[kLfFreq] == 400.0f, "format 4 lost its legacy LF dial");

    Values four;
    fkStoreParam(four.v, kHpfHz, 80.0f);
    fkStoreParam(four.v, kLpfFreq, 12800.0f);
    std::ostringstream out4;
    fkWriteUserPreset(out4, four.v);
    CHECK(out4.str().find("format_version=4") != std::string::npos, "this build does not write format 4");
    std::istringstream in4(out4.str());
    float read4[kParamCount];
    CHECK(fkReadUserPreset(in4, name, read4), "a format 4 preset was rejected");
    CHECK(read4[kHpfHz] == 80.0f && !fkFilterFollowsLegacyDial(read4, 0), "format 4 lost its Hz HPF");
    CHECK(fkFilterFollowsLegacyDial(read4, 1) && std::abs(read4[kLpfFreq] / 12800.0f - 1.0f) < 1.0e-4f,
          "format 4 lost its legacy LPF dial (%.1f)", read4[kLpfFreq]);
    std::istringstream in3("name=Three\nformat_version=3\nhm_hz=5000\n");
    float read3[kParamCount];
    CHECK(!fkReadUserPreset(in3, name, read3), "a format 3 preset, a version never released, was read");
    std::printf("[4] user presets: format 2 (effective_hz, control_hz) loads onto the saved dials; format 4\n"
                "    round-trips Hz and legacy bands and filters\n");
}
} // namespace

int main()
{
    std::printf("[1] last write wins\n");
    testLastWriteWins();
    testStatedSelector();
    testLegacyDialDefaults();
    testFactoryPresetsStateTheirHz();
    testReadoutIgnoresGain();
    testUserPresetFiles();
    testCurveDrawsWhatPlays();
    testSelectorWritesNeverLoseOneAnother();
    testSelectorWritesRace();
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

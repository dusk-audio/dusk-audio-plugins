// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The top bar's previous and next preset arrows, on the production editor: they
// step through the presets in the order the preset menu lists them (mode by
// mode, each mode's credited artist presets after its own), not in bank order.
// In bank order, next from Stab Machine (an Oracle preset) jumped to Pulsing
// Darkness (Mono); the Oracle column lists Upside Down after it.
#include "DafUI.hpp"   // the window-free host (shared-daf/ui/tests/daf-ui-test-host), first on the path
#include "MultiSynthUI.cpp"
#include "DuskValueFieldTest.hpp"
#include "MultiSynthTestBridge.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace
{
using Host = dusktest::EditorHost<DAF::MultiSynthUI>;

// Arrow centres in design pixels (MultiSynthUI kBarPrevX0 / kBarNextX0 plus half
// of kBarChevW, and the middle of kBarY0..kBarY1). At the design size a design
// coordinate is a window pixel.
constexpr float kPrevX = 792.0f + 17.0f, kNextX = 1042.0f + 17.0f, kArrowY = 28.0f;

// Every core parameter a factory preset sets, the way applyPreset() pushes them:
// defaults, then the bank baseline, then the preset's own rows.
std::vector<float> presetState(int idx)
{
    std::vector<float> v(kNumCoreParams);
    for (uint32_t i = 0; i < kNumCoreParams; ++i) v[i] = kParamDefs[i].def;
    for (int r = 0; r < kBaselineRows; ++r) v[kPresetBaseline[r].index] = kPresetBaseline[r].value;
    const FactoryPreset& pr = kFactoryPresets[idx];
    for (int r = 0; r < pr.nRows; ++r) v[pr.rows[r].index] = pr.rows[r].value;
    return v;
}

int presetMode(int idx) { return (int)std::lround(presetState(idx)[kParamMode]); }

int presetIndex(const char* name)
{
    for (int i = 0; i < kNumFactoryPresets; ++i)
        if (std::strcmp(kFactoryPresets[i].name, name) == 0) return i;
    return -1;
}

// Which factory preset the host was sent since event `start`: the one whose full
// state the writes reproduce, or -1. presetState() is distinct per preset (checked
// in main), so a match names exactly one.
int sentPreset(const Host& h, size_t start)
{
    std::vector<float> v(kNumCoreParams, -1.0e30f);
    bool wrote = false;
    for (size_t e = start; e < h.ui.hostEvents.size(); ++e)
    {
        const auto& ev = h.ui.hostEvents[e];
        if (ev.kind == dusktest::HostEvent::Write && ev.parameter < kNumCoreParams)
        { v[ev.parameter] = ev.value; wrote = true; }
    }
    if (!wrote) return -1;
    for (int i = 0; i < kNumFactoryPresets; ++i)
        if (presetState(i) == v) return i;
    return -1;
}

int press(Host& h, float x)
{
    const size_t start = h.ui.hostEvents.size();
    h.click(x, kArrowY);
    h.idle(2);
    return sentPreset(h, start);
}

std::string nameOf(int idx)
{
    return (idx >= 0 && idx < kNumFactoryPresets) ? kFactoryPresets[idx].name : "(none)";
}
} // namespace

int main()
{
    dusktest::isolateHome(SUNSET_UI_TEST_HOME);   // no user presets: the walk is the factory bank
    using dusktest::guards;

    bool distinct = true;
    for (int a = 0; a < kNumFactoryPresets; ++a)
        for (int b = a + 1; b < kNumFactoryPresets; ++b)
            if (presetState(a) == presetState(b)) distinct = false;
    guards(distinct, "every factory preset has a state of its own, so a host write names one preset");

    // The reported case.
    {
        Host h(1240, 780, {});
        const int stab = presetIndex("Stab Machine"), upside = presetIndex("Upside Down");
        h.ui.program((uint32_t)stab);   // as a host recalls it
        h.idle(2);
        const int next = press(h, kNextX);
        guards(next == upside, "next from Stab Machine loads Upside Down (got " + nameOf(next) + ")");
        const int back = press(h, kPrevX);
        guards(back == stab, "previous from Upside Down loads Stab Machine (got " + nameOf(back) + ")");
    }

    // Next from a fresh editor (nothing recalled), then once per preset: the walk
    // is the menu, and the arrow after the last entry wraps to the first.
    Host h(1240, 780, {});
    std::vector<int> walk;
    for (int k = 0; k < kNumFactoryPresets; ++k) walk.push_back(press(h, kNextX));
    const int wrapped = press(h, kNextX);
    guards(wrapped == walk.front(), "next after the last entry wraps to the first (got " + nameOf(wrapped) + ")");

    std::vector<int> seen(kNumFactoryPresets, 0);
    bool valid = true;
    for (int idx : walk) { if (idx < 0) valid = false; else ++seen[idx]; }
    bool once = valid;
    for (int c : seen) once = once && c == 1;
    guards(once, "next visits every factory preset exactly once");

    // The menu's order: modes COSMOS..ACID; within a mode its uncredited presets
    // in bank order, then each author's presets of that mode under their heading.
    bool ordered = once;
    for (size_t k = 1; ordered && k < walk.size(); ++k)
    {
        const int a = walk[k - 1], b = walk[k];
        const int ma = presetMode(a), mb = presetMode(b);
        const char* const ba = kFactoryPresets[a].author;
        const char* const bb = kFactoryPresets[b].author;
        if (ma != mb) { ordered = ma < mb; continue; }
        if (ba == nullptr && bb == nullptr) ordered = a < b;
        else if (ba != nullptr && bb == nullptr) ordered = false;   // credited before uncredited
        else if (ba != nullptr && bb != nullptr && std::strcmp(ba, bb) == 0) ordered = a < b;
        if (!ordered)
            std::printf("  out of menu order: %s then %s\n", nameOf(a).c_str(), nameOf(b).c_str());
    }
    guards(ordered, "next steps in menu order: by mode, artists last within their mode, bank order within a run");

    // Previous walks the same list backwards, wrapping from the first entry to the last.
    bool reversed = once;
    for (int k = kNumFactoryPresets - 1; reversed && k >= 0; --k)
    {
        const int got = press(h, kPrevX);
        const int want = walk[(size_t)k];
        if (got != want)
        {
            std::printf("  previous gave %s, expected %s\n", nameOf(got).c_str(), nameOf(want).c_str());
            reversed = false;
        }
    }
    guards(reversed, "previous retraces the menu order backwards and wraps to the last entry");

    {
        Host fresh(1240, 780, {});
        const int last = press(fresh, kPrevX);
        guards(once && last == walk.back(), "previous with nothing recalled loads the menu's last entry (got "
                                            + nameOf(last) + ")");
    }

    return dusktest::summary();
}
